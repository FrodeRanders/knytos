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
    ok = check_reuse(Start + 750),
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
    erlang:display({m4, active_cancel, pass}),
    ok.

check_reuse(Deadline) ->
    true = erlang:monotonic_time(millisecond) < Deadline,
    case knytos:request(<<"m4.hold">>, 2, 1) of
        {error, busy} ->
            receive after 20 -> check_reuse(Deadline) end;
        {ok, Reference} ->
            {ok, 42} = result(Reference),
            ok
    end.

result(Reference) ->
    receive {knytos_reply, Reference, Result} -> Result
    after 5000 -> error(missing_reply)
    end.
