%% SPDX-License-Identifier: Apache-2.0
-module(native_service).
-export([start/0]).

start() ->
    try knytos:request(<<"time">>, -1, 0) of
        _ -> error(invalid_opcode_accepted)
    catch
        error:badarg -> ok
    end,
    {ok, First} = knytos:request(<<"time">>, 255, 0),
    {error, busy} = knytos:request(<<"time">>, 255, 0),
    %% Time's unknown-opcode reply is scalar and independent of UTC sync.
    {ok, -3} = result(First),
    {error, not_found} = knytos:cancel(First),
    erlang:display({m4, scalar_reply}),
    {ok, Denied} = knytos:request(<<"rng">>, 255, 0),
    {error, unauthorized} = result(Denied),
    erlang:display({m4, unauthorized}),
    {ok, Attachment} = knytos:request(<<"observe">>, 1, 0),
    {error, unexpected_reply} = result(Attachment),
    erlang:display({m4, attachment_rejected}),
    {ok, Slow} = knytos:request(<<"m4.missing">>, 255, 0),
    receive after 25 -> ok end,
    {error, busy} = knytos:request(<<"time">>, 255, 0),
    erlang:display({m4, timer_with_pending_call}),
    {error, SlowReason} = result(Slow),
    true = SlowReason =:= timeout orelse SlowReason =:= unavailable,
    ok = cancel_running(),
    ok = cancel_immediately(),
    Parent = self(),
    {Child, Monitor} = spawn_opt(fun() ->
        {ok, _} = knytos:request(<<"m4.missing">>, 255, 0),
        Parent ! accepted
    end, [monitor]),
    receive accepted -> ok after 5000 -> error(child_not_accepted) end,
    receive {'DOWN', Monitor, process, Child, normal} -> ok
    after 5000 -> error(child_still_alive)
    end,
    %% A timer can run while the abandoned lookup's worker waits. Its
    %% bounded native budget ends before this fixture checks slot reuse.
    receive after 1500 -> ok end,
    {ok, Last} = knytos:request(<<"time">>, 255, 0),
    {ok, -3} = result(Last),
    erlang:display({m4, abandoned_slot_reused}),
    erlang:display({m4, native_service, pass}),
    ok.

result(Reference) ->
    receive {knytos_reply, Reference, Result} -> Result
    after 5000 -> error(missing_reply)
    end.

cancel_running() ->
    {ok, Reference} = knytos:request(<<"m4.missing">>, 255, 0),
    Parent = self(),
    spawn_opt(fun() -> Parent ! {foreign_cancel, knytos:cancel(Reference)} end, []),
    receive {foreign_cancel, {error, not_found}} -> ok
    after 5000 -> error(foreign_cancel_authorized)
    end,
    erlang:display({m4, foreign_cancel_rejected}),
    receive after 25 -> ok end,
    {error, busy} = knytos:request(<<"time">>, 255, 0),
    ok = knytos:cancel(Reference),
    check_replacement_after_cancel(),
    expect_no_reply(Reference).

cancel_immediately() ->
    {ok, Reference} = knytos:request(<<"time">>, 255, 0),
    ok = knytos:cancel(Reference),
    check_replacement_after_cancel(),
    expect_no_reply(Reference),
    {ok, Later} = knytos:request(<<"time">>, 255, 0),
    {ok, -3} = result(Later),
    erlang:display({m4, cancelled_slot_reused}),
    ok.

check_replacement_after_cancel() ->
    case knytos:request(<<"time">>, 255, 0) of
        {error, busy} -> erlang:display({m4, cancel_slot_busy});
        {ok, Later} ->
            {ok, -3} = result(Later),
            erlang:display({m4, cancel_slot_released})
    end.

expect_no_reply(Reference) ->
    receive {knytos_reply, Reference, _} -> error(cancelled_reply_delivered)
    after 1500 -> ok
    end,
    {error, not_found} = knytos:cancel(Reference),
    erlang:display({m4, cancelled_reply_suppressed}),
    ok.
