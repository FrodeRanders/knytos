%% SPDX-License-Identifier: Apache-2.0
-module(utc_unavailable).
-export([start/0]).

start() ->
    %% Expected explicit domain failure: the upstream void clock hook cannot
    %% represent unavailable UTC, and this profile has no time-service client.
    erlang:system_time(millisecond),
    error(unexpected_wall_clock_success).
