%% SPDX-License-Identifier: Apache-2.0
-module(schedulers).
-export([start/0]).

start() ->
    Count = erlang:system_info(schedulers_online),
    Count = erlang:system_info(schedulers),
    erlang:display({schedulers, Count}),
    case atomvm:platform() of
        knytos -> ok = fixed_roster();
        _ -> ok
    end,
    ok = wave(),
    %% Leave the VM idle so the elected poller must deliver a real deadline.
    receive after 30 -> ok end,
    ok = wave(),
    erlang:display({smp, pass}),
    ok.

fixed_roster() ->
    try erlang:system_flag(schedulers_online, 1) of
        _ -> error(scheduler_count_changed)
    catch
        error:badarg -> ok
    end.

wave() ->
    Tag = make_ref(),
    launch(12, self(), Tag),
    collect(12, Tag, #{}).

launch(0, _Parent, _Tag) -> ok;
launch(N, Parent, Tag) ->
    spawn_opt(fun() ->
        receive after 5 -> ok end,
        Parent ! {Tag, N, sum(3000, 0)}
    end, []),
    launch(N - 1, Parent, Tag).

collect(0, _Tag, Seen) when map_size(Seen) =:= 12 -> ok;
collect(N, Tag, Seen) ->
    receive
        {Tag, Index, 4501500} when Index >= 1, Index =< 12 ->
            case Seen of
                #{Index := true} -> error({duplicate_worker, Index});
                _ -> collect(N - 1, Tag, Seen#{Index => true})
            end
    after 2000 ->
        error({missing_workers, N})
    end.

sum(0, Acc) -> Acc;
sum(N, Acc) -> sum(N - 1, Acc + N).
