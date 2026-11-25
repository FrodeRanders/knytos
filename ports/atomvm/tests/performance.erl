%% SPDX-License-Identifier: Apache-2.0
-module(performance).
-export([start/0]).

-define(PAIRS, 12).
-define(ROUNDS, 4000).
-define(WINDOW_MS, 8000).
-define(TIMER_MS, 10).
-define(SAMPLES, 32).

start() ->
    Count = erlang:system_info(schedulers_online),
    erlang:display({perf, schedulers, Count}),
    start_pairs(?PAIRS, self()),
    Clients = await_clients(?PAIRS, []),
    run_pairs(Clients, 500),
    trials(1, Clients),
    Probe = spawn_opt(fun() -> wake_peer() end, []),
    sample(1, Probe),
    Probe ! stop,
    IdleStart = now_us(),
    erlang:display({perf, idle_start, IdleStart}),
    sleep(?WINDOW_MS),
    erlang:display({perf, idle_end, now_us()}),
    LoadStart = now_us(),
    erlang:display({perf, load_start, LoadStart}),
    Batches = load(Clients, LoadStart + ?WINDOW_MS * 1000, 0),
    erlang:display({perf, load_end, now_us(), Batches}),
    stop_pairs(Clients),
    erlang:display({perf, pass}),
    ok.

now_us() -> erlang:monotonic_time(microsecond).
sleep(Ms) -> receive after Ms -> ok end.

start_pairs(0, _Parent) -> ok;
start_pairs(N, Parent) ->
    spawn_opt(fun() ->
        Client = self(),
        Echo = spawn_opt(fun() -> echo(Client) end, []),
        Parent ! {ready, Client},
        client(Parent, Echo)
    end, []),
    start_pairs(N - 1, Parent).

await_clients(0, Clients) -> Clients;
await_clients(N, Clients) ->
    receive {ready, Pid} -> await_clients(N - 1, [Pid | Clients])
    after 5000 -> error(pair_start_timeout)
    end.

echo(Client) ->
    receive
        {Tag, Seq} -> Client ! {Tag, Seq}, echo(Client);
        stop -> Client ! echo_stopped
    end.

client(Parent, Echo) ->
    receive
        {run, Tag, Rounds} ->
            exchange(Echo, Tag, Rounds),
            Parent ! {done, Tag, self()},
            client(Parent, Echo);
        stop ->
            Echo ! stop,
            receive echo_stopped -> Parent ! {stopped, self()} end
    end.

exchange(_Echo, _Tag, 0) -> ok;
exchange(Echo, Tag, Seq) ->
    Echo ! {Tag, Seq},
    receive {Tag, Seq} -> exchange(Echo, Tag, Seq - 1) end.

run_pairs(Clients, Rounds) ->
    Tag = make_ref(),
    lists:foreach(fun(Pid) -> Pid ! {run, Tag, Rounds} end, Clients),
    lists:foreach(fun(Pid) ->
        receive {done, Tag, Pid} -> ok
        after 20000 -> error({message_batch_timeout, Pid})
        end
    end, Clients).

trials(4, _Clients) -> ok;
trials(Index, Clients) ->
    Started = now_us(),
    run_pairs(Clients, ?ROUNDS),
    erlang:display({perf, throughput, Index, 2 * ?PAIRS * ?ROUNDS, now_us() - Started}),
    trials(Index + 1, Clients).

wake_peer() ->
    receive
        {arm, Parent, Tag} ->
            Parent ! {armed, Tag},
            receive {wake, Tag, Sent} -> Parent ! {latency, Tag, now_us() - Sent} end,
            wake_peer();
        stop -> ok
    end.

sample(Index, _Peer) when Index > ?SAMPLES -> ok;
sample(Index, Peer) ->
    Tag = make_ref(),
    Peer ! {arm, self(), Tag},
    receive {armed, Tag} -> ok after 5000 -> error(arm_timeout) end,
    Before = now_us(),
    sleep(?TIMER_MS),
    Late = now_us() - Before - ?TIMER_MS * 1000,
    true = Late >= 0,
    Peer ! {wake, Tag, now_us()},
    receive {latency, Tag, Gap} when Gap >= 0 ->
        erlang:display({perf, latency, Index, Late, Gap})
    after 5000 -> error(wake_timeout)
    end,
    sample(Index + 1, Peer).

load(Clients, Deadline, Batches) ->
    run_pairs(Clients, ?ROUNDS),
    case now_us() < Deadline of
        true -> load(Clients, Deadline, Batches + 1);
        false -> Batches + 1
    end.

stop_pairs(Clients) ->
    lists:foreach(fun(Pid) -> Pid ! stop end, Clients),
    lists:foreach(fun(Pid) ->
        receive {stopped, Pid} -> ok after 5000 -> error(pair_stop_timeout) end
    end, Clients).
