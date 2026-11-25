%% SPDX-License-Identifier: Apache-2.0
-module(active_cancel).
-export([start/0]).

start() ->
    Start = erlang:monotonic_time(millisecond),
    {ok, Cancelled} = knytos:request(<<"m4.hold">>, 1, 1),
    %% The signed runner checks the server's accepted marker before this
    %% cancel marker. The service holds its reply until the replacement call.
    receive after 100 -> ok end,
    erlang:display({m4, cancelling_accepted_call}),
    ok = knytos:cancel(Cancelled),
    ok = check_reuse(Start + 750, 1),
    Elapsed = erlang:monotonic_time(millisecond) - Start,
    true = Elapsed < 750,
    erlang:display({m4, accepted_cancel_reused_ms, Elapsed}),

    {ok, TimedOut} = knytos:request(<<"m4.hold">>, 1, 2),
    {error, timeout} = result(TimedOut),
    {ok, Probe} = knytos:request(<<"m4.hold">>, 2, 2),
    {ok, 42} = result(Probe),
    erlang:display({m4, accepted_call_timed_out}),
    receive {knytos_reply, Cancelled, _} -> error(cancelled_reply_delivered)
    after 0 -> ok
    end,
    ok = owner_exit(3, normal),
    ok = owner_exit(4, killed),
    erlang:display({m4, active_cancel, pass}),
    ok.

owner_exit(Case, Reason) ->
    Parent = self(),
    Start = erlang:monotonic_time(millisecond),
    {Owner, Monitor} = spawn_opt(fun() ->
        {ok, Ref} = knytos:request(<<"m4.hold">>, 1, Case),
        Parent ! submitted,
        receive
            finish -> ok;
            {knytos_reply, Ref, _} -> error(call_completed_while_held)
        after 5000 -> error(owner_still_running)
        end
    end, [monitor]),
    receive submitted -> ok after 5000 -> error(owner_did_not_submit) end,
    receive after 100 -> ok end,
    {Bystander, OtherMonitor} = spawn_opt(fun() -> ok end, [monitor]),
    receive {'DOWN', OtherMonitor, process, Bystander, normal} -> ok
    after 5000 -> error(bystander_still_running)
    end,
    {error, busy} = knytos:request(<<"m4.hold">>, 2, Case),
    erlang:display({m4, releasing_request_owner, Case}),
    case Reason of
        normal -> Owner ! finish;
        killed -> true = exit(Owner, kill)
    end,
    receive {'DOWN', Monitor, process, Owner, Reason} -> ok
    after 5000 -> error(owner_still_alive)
    end,
    ok = check_reuse(Start + 750, Case),
    Elapsed = erlang:monotonic_time(millisecond) - Start,
    true = Elapsed < 750,
    erlang:display({m4, owner_exit_reused_ms, Case, Elapsed}),
    ok.

check_reuse(Deadline, Case) ->
    true = erlang:monotonic_time(millisecond) < Deadline,
    case knytos:request(<<"m4.hold">>, 2, Case) of
        {error, busy} ->
            receive after 20 -> check_reuse(Deadline, Case) end;
        {ok, Reference} ->
            {ok, 42} = result(Reference),
            ok
    end.

result(Reference) ->
    receive {knytos_reply, Reference, Result} -> Result
    after 5000 -> error(missing_reply)
    end.
