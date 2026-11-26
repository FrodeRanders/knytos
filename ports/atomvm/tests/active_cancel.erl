%% SPDX-License-Identifier: Apache-2.0
-module(active_cancel).
-export([start/0]).

start() ->
    Start = erlang:monotonic_time(millisecond),
    {ok, Cancelled} = knytos:request(<<"m4.hold">>, 1, 1, <<0, 255>>),
    %% The signed runner checks the server's accepted marker before this
    %% cancel marker. The service holds its reply until the replacement call.
    receive after 100 -> ok end,
    erlang:display({m4, cancelling_accepted_call}),
    ok = knytos:cancel(Cancelled),
    ok = check_reuse(Start + 750, 1),
    Elapsed = erlang:monotonic_time(millisecond) - Start,
    true = Elapsed < 750,
    erlang:display({m4, accepted_cancel_reused_ms, Elapsed}),

    {ok, TimedOut} = knytos:request(<<"m4.hold">>, 1, 2, <<>>),
    {error, timeout} = result(TimedOut),
    {ok, Probe} = knytos:request(<<"m4.hold">>, 2, 2),
    {ok, 42} = result(Probe),
    erlang:display({m4, accepted_call_timed_out}),
    receive {knytos_reply, Cancelled, _} -> error(cancelled_reply_delivered)
    after 0 -> ok
    end,
    ok = owner_exit(3, normal),
    ok = owner_exit(4, killed),
    ok = payload_cases(),
    ok = subscription_cases(),
    erlang:display({m4, active_cancel, pass}),
    ok.

payload_cases() ->
    Name = <<"m4.hold">>,
    {ok, Denied} = knytos:request(<<"time">>, 255, 0, <<1>>),
    {error, unauthorized} = result(Denied),
    {ok, Lookup} = knytos:lookup(Name),
    {ok, Handle} = result(Lookup),
    ok = echo(Name, <<0, 255, 17>>),
    ok = echo(Handle, <<>>),
    Page = binary:copy(<<3, 254>>, 2048),
    ok = echo(Handle, Page),
    {ok, Scalar} = knytos:request(Handle, 255, 0, <<1>>),
    {ok, -1} = result(Scalar),
    erlang:display({m4, byte_payload_roundtrip}),
    {ok, Negative} = knytos:request(Handle, 3, -1, <<1>>),
    {error, unexpected_reply} = result(Negative),
    {ok, Oversized} = knytos:request(Handle, 3, 4097, <<1>>),
    {error, unexpected_reply} = result(Oversized),
    {ok, LargeBacking} = knytos:request(Handle, 4, 0, <<1>>),
    {error, unexpected_reply} = result(LargeBacking),
    TooLarge = <<Page/binary, 0>>,
    try knytos:request(Handle, 3, 4097, TooLarge) of
        _ -> error(oversized_payload_admitted)
    catch
        error:badarg -> ok
    end,
    ok = echo(Handle, <<42>>),
    ok = knytos:close(Handle),
    erlang:display({m4, byte_payload_bounds}),
    ok.

echo(Service, Bytes) ->
    {ok, Ref} = knytos:request(Service, 3, byte_size(Bytes), Bytes),
    {ok, Bytes} = result(Ref),
    ok.

subscription_cases() ->
    Parent = self(),
    {Owner, OwnerMonitor} = spawn_opt(fun() ->
        Handle = lookup_hold(erlang:monotonic_time(millisecond) + 750),
        Ref = subscribe_ready(Handle, erlang:monotonic_time(millisecond) + 750),
        Parent ! {abandoned_watch, Ref}
    end, [monitor]),
    Abandoned = receive {abandoned_watch, Ref} -> Ref after 5000 -> error(no_watch) end,
    receive {'DOWN', OwnerMonitor, process, Owner, normal} -> ok
    after 5000 -> error(watch_owner_still_alive)
    end,
    Handle = lookup_hold(erlang:monotonic_time(millisecond) + 750),
    {error, not_found} = knytos:unsubscribe(Abandoned),
    {ok, Pending} = knytos:subscribe(Handle, closed),
    ok = knytos:unsubscribe(Pending),
    receive {knytos_reply, Pending, ok} -> ok after 0 -> ok end,
    Old = subscribe_ready(Handle, erlang:monotonic_time(millisecond) + 750),
    {error, busy} = knytos:subscribe(Handle, closed),
    {Peer, PeerMonitor} = spawn_opt(fun() ->
        {error, not_found} = knytos:unsubscribe(Old),
        {error, not_found} = knytos:subscribe(Handle, closed)
    end, [monitor]),
    receive {'DOWN', PeerMonitor, process, Peer, normal} -> ok
    after 5000 -> error(foreign_watch_access)
    end,
    ok = knytos:unsubscribe(Old),
    {error, not_found} = knytos:unsubscribe(Old),
    Watch = subscribe_ready(Handle, erlang:monotonic_time(millisecond) + 750),
    erlang:display({m4, close_watch_scope}),
    1 = deployment_sequence(Handle),
    erlang:display({m4, awaiting_replacement}),
    ok = wait_closed(Watch),
    {error, not_found} = knytos:request(Handle, 6, 0),
    {error, not_found} = knytos:unsubscribe(Watch),
    erlang:display({m4, replaced_handle_stale}),

    Replacement = lookup_hold(erlang:monotonic_time(millisecond) + 750),
    2 = deployment_sequence(Replacement),
    {error, not_found} = knytos:request(Handle, 6, 0),
    {error, not_found} = knytos:subscribe(Handle, closed),
    ok = echo(Replacement, <<2, 255>>),
    Current = subscribe_ready(Replacement, erlang:monotonic_time(millisecond) + 750),
    {error, not_found} = knytos:unsubscribe(Watch),
    erlang:display({m4, replacement_reacquired}),

    {ok, Close} = knytos:request(Replacement, 5, 0),
    {ok, 0} = result(Close),
    erlang:display({m4, awaiting_close_watch}),
    ok = wait_closed(Current),
    {error, not_found} = knytos:request(Replacement, 255, 0),
    {error, not_found} = knytos:unsubscribe(Current),
    receive
        {knytos_event, _, _} -> error(unexpected_watch_event);
        {knytos_reply, Pending, _} -> error(cancelled_registration_delivered)
    after 20 -> ok
    end,
    erlang:display({m4, close_watch_event}),
    ok.

deployment_sequence(Handle) ->
    {ok, Ref} = knytos:request(Handle, 6, 0),
    {ok, Sequence} = result(Ref),
    Sequence.

wait_closed(Watch) ->
    receive
        {knytos_event, Watch, closed} -> ok;
        {knytos_event, Watch, {error, Reason}} -> error({watch_failed, Reason})
    end.

lookup_hold(Deadline) ->
    true = erlang:monotonic_time(millisecond) < Deadline,
    case knytos:lookup(<<"m4.hold">>) of
        {error, busy} -> receive after 20 -> lookup_hold(Deadline) end;
        {ok, Ref} ->
            {ok, Handle} = result(Ref),
            Handle
    end.

subscribe_ready(Handle, Deadline) ->
    true = erlang:monotonic_time(millisecond) < Deadline,
    case knytos:subscribe(Handle, closed) of
        {error, busy} -> receive after 20 -> subscribe_ready(Handle, Deadline) end;
        {ok, Ref} ->
            ok = result(Ref),
            Ref
    end.

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
