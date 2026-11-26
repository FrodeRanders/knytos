%% SPDX-License-Identifier: Apache-2.0
-module(active_cancel).
-export([start/0]).

start() ->
    Start = erlang:monotonic_time(millisecond),
    {ok, Cancelled} = knytos:request(<<"m4.hold">>, 1, 1, <<0, 255>>),
    %% The signed runner checks the server's accepted marker before this
    %% cancel marker. The service holds its reply until the replacement call.
    receive after 100 -> ok end,
    {error, busy} = knytos:call(<<"m4.hold">>, {255, 0}, 100),
    erlang:display({m4, cancelling_accepted_call}),
    ok = knytos:cancel(Cancelled),
    ok = check_reuse(Start + 750, 1),
    Elapsed = erlang:monotonic_time(millisecond) - Start,
    true = Elapsed < 750,
    erlang:display({m4, accepted_cancel_reused_ms, Elapsed}),

    {error, timeout} = knytos:call(<<"m4.hold">>, {1, 2, <<>>}),
    {ok, Probe} = knytos:request(<<"m4.hold">>, 2, 2),
    {ok, 42} = result(Probe),
    erlang:display({m4, accepted_call_timed_out}),
    receive {knytos_reply, Cancelled, _} -> error(cancelled_reply_delivered)
    after 0 -> ok
    end,
    ok = owner_exit(3, normal),
    ok = owner_exit(4, killed),
    ok = call_cases(),
    ok = payload_cases(),
    ok = mailbox_pressure(),
    ok = subscription_cases(),
    erlang:display({m4, active_cancel, pass}),
    ok.

call_cases() ->
    Name = <<"m4.hold">>,
    {ok, -1} = knytos:call(Name, {255, 0}),
    {ok, <<0, 255>>} = knytos:call(Name, {3, 2, <<0, 255>>}, 500),
    {error, unauthorized} = knytos:call(<<"time">>, {255, 0}, 500),
    ok = expect_bad_timeout(Name, -1),
    ok = expect_bad_timeout(Name, 16#100000000),
    ok = expect_bad_timeout(Name, invalid),
    erlang:display({m4, sync_call_results}),

    Unrelated = make_ref(),
    self() ! {knytos_reply, Unrelated, untouched},
    Start = erlang:monotonic_time(millisecond),
    {error, timeout} = knytos:call(Name, {1, 5, <<5>>}, 100),
    erlang:display({m4, sync_call_timeout}),
    ok = check_reuse(Start + 750, 5),
    receive {knytos_reply, Unrelated, untouched} -> ok
    after 0 -> error(unrelated_reply_removed)
    end,
    receive {knytos_reply, _, _} -> error(sync_reply_leaked) after 20 -> ok end,
    erlang:display({m4, sync_call_mailbox_clean}),
    ok.

expect_bad_timeout(Name, Timeout) ->
    try knytos:call(Name, {1, 5, <<5>>}, Timeout) of
        _ -> error(bad_timeout_admitted)
    catch
        error:badarg -> ok
    end.

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
    {ok, <<42>>} = knytos:call(Handle, {3, 1, <<42>>}, 500),
    ok = knytos:close(Handle),
    erlang:display({m4, byte_payload_bounds}),
    ok.

echo(Service, Bytes) ->
    {ok, Ref} = knytos:request(Service, 3, byte_size(Bytes), Bytes),
    {ok, Bytes} = result(Ref),
    ok.

mailbox_pressure() ->
    Name = <<"m4.hold">>,
    Parent = self(),
    {Holder, Monitor} = spawn_opt(fun() ->
        Fake = make_ref(),
        self() ! {knytos_reply, Fake, {ok, -1}},
        Refs = queue_unread(Name, 8),
        {ok, -1} = result(Fake),
        {error, busy} = knytos:request(Name, 255, 0),
        Parent ! {mailbox_full, self()},
        receive take_one -> ok after 5000 -> error(no_take_one) end,
        {ok, -1} = result(lists:last(Refs)),
        Parent ! {one_received, self()},
        receive finish -> ok after 5000 -> error(no_finish) end
    end, [monitor]),
    receive {mailbox_full, Holder} -> ok after 5000 -> error(mailbox_not_full) end,
    {error, busy} = knytos:call(Name, {255, 0}),
    {error, busy} = knytos:lookup(Name),
    Holder ! take_one,
    receive {one_received, Holder} -> ok after 5000 -> error(no_receive_credit) end,
    {ok, -1} = knytos:call(Name, {255, 0}),
    Holder ! finish,
    receive {'DOWN', Monitor, process, Holder, normal} -> ok
    after 5000 -> error(holder_still_alive)
    end,
    Refilled = queue_unread(Name, 8),
    {error, busy} = knytos:request(Name, 255, 0),
    ok = drain_replies(Refilled),
    erlang:display({m4, mailbox_backpressure}),
    ok.

queue_unread(Service, Count) ->
    {message_queue_len, Depth} = erlang:process_info(self(), message_queue_len),
    queue_unread(Service, Count, Depth, erlang:monotonic_time(millisecond) + 2000).

queue_unread(_, 0, _, _) ->
    [];
queue_unread(Service, Count, Depth, Deadline) ->
    {ok, Ref} = knytos:request(Service, 255, 0),
    ok = wait_for_mailbox(Depth + 1, Deadline),
    [Ref | queue_unread(Service, Count - 1, Depth + 1, Deadline)].

%% This fixture observes queue depth without consuming the replies under test.
wait_for_mailbox(Expected, Deadline) ->
    case erlang:process_info(self(), message_queue_len) of
        {message_queue_len, Expected} -> ok;
        {message_queue_len, Depth} when Depth < Expected ->
            true = erlang:monotonic_time(millisecond) < Deadline,
            receive after 5 -> wait_for_mailbox(Expected, Deadline) end
    end.

drain_replies(Refs) ->
    lists:foreach(fun(Ref) -> {ok, -1} = result(Ref) end, Refs).

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
    [One | Others] = queue_unread(Handle, 7),
    {error, busy} = knytos:subscribe(Handle, closed),
    {ok, -1} = result(One),
    Old = subscribe_ready(Handle, erlang:monotonic_time(millisecond) + 750),
    ok = drain_replies(Others),
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
    BeforeReplacement = queue_unread(Handle, 7),
    {error, busy} = knytos:request(Handle, 255, 0),
    erlang:display({m4, awaiting_replacement}),
    ok = wait_closed(Watch),
    {message_queue_len, 7} = erlang:process_info(self(), message_queue_len),
    ok = drain_replies(BeforeReplacement),
    erlang:display({m4, watch_delivery_reserved}),
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
