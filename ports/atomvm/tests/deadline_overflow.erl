%% SPDX-License-Identifier: Apache-2.0
-module(deadline_overflow).
-export([start/0]).

start() ->
    %% This duration fits uint64_t nanoseconds alone. Adding a boot-time
    %% monotonic sample after the first millisecond must reject it.
    ok = receive after 1 -> ok end,
    receive after 18446744073709 -> error(wrapped_deadline) end.
