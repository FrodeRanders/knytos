%% SPDX-License-Identifier: Apache-2.0
-module(semantics).
-export([start/0, init/1, start_worker/1, handle_call/3, handle_cast/2]).

start() ->
    ok = values(),
    erlang:display({m3, values, ok}),
    ok = process_semantics(),
    erlang:display({m3, processes, ok}),
    ok = timers(),
    erlang:display({m3, timers, ok}),
    ok = supervision(),
    erlang:display({m3, supervision, ok}),
    erlang:display({m3, pass}),
    ok.

values() ->
    [4, 3, 2, 1] = sequence(4),
    {1, 2} = quotient_remainder(15, 13),
    -7 = negate(7),
    Bytes = packet(258, <<"hi">>),
    #{key := {match, [258, <<"hi">>]}} = unpack(Bytes),
    ok =
        try reject(unsupported)
        catch throw:{rejected, unsupported} -> ok
        end,
    Retained = {sequence(512), Bytes},
    true = erlang:garbage_collect(),
    {512, Bytes} = after_collection(Retained),
    ok.

sequence(0) -> [];
sequence(N) -> [N | sequence(N - 1)].
quotient_remainder(A, B) -> {A div B, A rem B}.
negate(N) -> 0 - N.
packet(N, Body) -> <<N:16/big, Body/binary>>.
unpack(<<N:16/big, Body/binary>>) -> #{key => {match, [N, Body]}}.
reject(Value) -> throw({rejected, Value}).
after_collection({List, Bytes}) -> {length(List), Bytes}.

process_semantics() ->
    Parent = self(),
    OriginalTrap = process_flag(trap_exit, true),
    Worker = spawn_link(fun() ->
        receive
            {reply, Tag} ->
                Parent ! {unmatched, Tag},
                Parent ! {matched, Tag},
                exit(fixture_exit)
        end
    end),
    true = register(m3_worker, Worker),
    Worker = whereis(m3_worker),
    Monitor = monitor(process, Worker),
    Tag = make_ref(),
    m3_worker ! {reply, Tag},
    receive {matched, Tag} -> ok after 2000 -> error(matched_timeout) end,
    receive {unmatched, Tag} -> ok after 0 -> error(message_lost) end,
    receive {'DOWN', Monitor, process, Worker, fixture_exit} -> ok
    after 2000 -> error(monitor_timeout)
    end,
    receive {'EXIT', Worker, fixture_exit} -> ok
    after 2000 -> error(link_timeout)
    end,
    undefined = whereis(m3_worker),
    false = is_process_alive(Worker),
    true = process_flag(trap_exit, OriginalTrap),
    ok.

timers() ->
    Parent = self(),
    Tag = make_ref(),
    %% A message cancels an armed receive timer. Then keep the VM alive beyond
    %% the original deadline while checking fresh waits on the same process.
    spawn(fun() -> receive after 20 -> Parent ! {scheduled, Tag} end end),
    receive {scheduled, Tag} -> ok after 150 -> error(cancel_timeout) end,
    repeat_wait(4),
    %% Two distinct processes arm equally long timers. Observe both, without
    %% assuming a tie order or that their absolute deadlines coincide.
    A = start_timer(Parent),
    B = start_timer(Parent),
    receive {fired, A} -> ok after 2000 -> error(first_timer) end,
    receive {fired, B} -> ok after 2000 -> error(second_timer) end,
    %% A duration larger than the upstream platform wait's signed int limit
    %% stays representable and can still be cancelled by a message.
    Long = spawn(fun() ->
        Parent ! {armed, self()},
        receive stop -> Parent ! {cancelled, self()}
        after 3000000000 -> error(long_timer_fired)
        end
    end),
    receive {armed, Long} -> ok after 2000 -> error(long_timer_arm) end,
    %% Give the worker a scheduling opportunity to enter its timed receive.
    receive after 5 -> ok end,
    Long ! stop,
    receive {cancelled, Long} -> ok after 2000 -> error(long_timer_cancel) end,
    ok.

repeat_wait(0) -> ok;
repeat_wait(Count) ->
    Started = erlang:monotonic_time(millisecond),
    receive after 45 -> ok end,
    true = erlang:monotonic_time(millisecond) - Started >= 45,
    repeat_wait(Count - 1).

start_timer(Parent) ->
    spawn(fun() -> receive after 25 -> Parent ! {fired, self()} end end).

supervision() ->
    {ok, Supervisor} = supervisor:start_link(?MODULE, {supervisor, self()}),
    First = ready_worker(),
    {pong, First} = gen_server:call(First, ping, 2000),
    Monitor = monitor(process, First),
    ok = gen_server:cast(First, fail),
    receive {'DOWN', Monitor, process, First, fixture_failure} -> ok
    after 2000 -> error(worker_did_not_fail)
    end,
    Replacement = ready_worker(),
    true = First =/= Replacement,
    {pong, Replacement} = gen_server:call(Replacement, ping, 2000),
    ok = gen_server:stop(Replacement, normal, 2000),
    ok = gen_server:stop(Supervisor, normal, 2000),
    ok.

ready_worker() ->
    receive {ready, Pid} -> Pid after 2000 -> error(worker_not_ready) end.

init({supervisor, Parent}) ->
    Child = {worker, {?MODULE, start_worker, [Parent]}, transient, brutal_kill, worker, [?MODULE]},
    {ok, {{one_for_one, 2, 5}, [Child]}};
init({worker, Parent}) ->
    Parent ! {ready, self()},
    {ok, worker}.

start_worker(Parent) ->
    gen_server:start_link(?MODULE, {worker, Parent}, []).

handle_call(ping, _From, State) ->
    {reply, {pong, self()}, State}.

handle_cast(fail, State) ->
    {stop, fixture_failure, State}.
