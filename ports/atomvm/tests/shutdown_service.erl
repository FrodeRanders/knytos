%% SPDX-License-Identifier: Apache-2.0
-module(shutdown_service).
-export([start/0]).

start() ->
    {ok, Lookup} = knytos:lookup(<<"time">>),
    receive {knytos_reply, Lookup, {ok, Service}} -> start_busy(Service)
    after 5000 -> error(service_lookup_failed)
    end.

start_busy(Service) ->
    put(service, Service),
    Parent = self(),
    spawn_opt(fun() ->
        Reference = submit(),
        Parent ! accepted,
        request_forever(Reference)
    end, []),
    receive accepted -> ok after 5000 -> error(service_worker_unavailable) end,
    erlang:display({m3_shutdown, busy}),
    busy().

submit() ->
    {ok, Reference} = knytos:request(<<"m4.missing">>, 255, 0),
    Reference.

request_forever(Reference) ->
    receive {knytos_reply, Reference, {error, _}} -> request_forever(submit())
    after 5000 -> error(missing_reply)
    end.

busy() -> busy().
