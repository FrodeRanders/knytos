%% SPDX-License-Identifier: Apache-2.0
-module(shutdown_service).
-export([start/0]).

start() ->
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
