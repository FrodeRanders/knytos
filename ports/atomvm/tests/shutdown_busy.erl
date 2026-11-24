%% SPDX-License-Identifier: Apache-2.0
-module(shutdown_busy).
-export([start/0]).

start() ->
    start_workers(16, self()),
    await_workers(16),
    erlang:display({m3_shutdown, busy}),
    spin(0).

start_workers(0, _Parent) -> ok;
start_workers(N, Parent) ->
    spawn_opt(fun() -> Parent ! running, spin(0) end, []),
    start_workers(N - 1, Parent).

await_workers(0) -> ok;
await_workers(N) ->
    receive running -> await_workers(N - 1)
    after 5000 -> error(worker_start_timeout)
    end.

%% Keep each Erlang process runnable across interpreter reduction slices.
spin(N) ->
    spin((N + 1) band 65535).
