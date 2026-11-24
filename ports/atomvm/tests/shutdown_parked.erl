%% SPDX-License-Identifier: Apache-2.0
-module(shutdown_parked).
-export([start/0]).

start() ->
    erlang:display({m3_shutdown, parked}),
    receive stop -> ok end.
