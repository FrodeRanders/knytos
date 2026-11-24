%% SPDX-License-Identifier: Apache-2.0
-module(hello).
-export([start/0]).

start() ->
    Platform = atomvm:platform(),
    case Platform of knytos -> ok; generic_unix -> ok end,
    io:format("ATOMVM platform=~p~n", [Platform]),
    io:format("Hello from Erlang on Knytos~n"),
    Self = self(),
    %% The message is queued before receive: runnable work must not be lost.
    Self ! wake_before_receive,
    receive wake_before_receive -> ok after 1000 -> error(lost_wakeup) end,
    Before = erlang:monotonic_time(millisecond),
    receive after 25 -> ok end,
    Elapsed = erlang:monotonic_time(millisecond) - Before,
    true = Elapsed >= 25,
    io:format("ATOMVM PASS elapsed_ms=~p~n", [Elapsed]),
    ok.
