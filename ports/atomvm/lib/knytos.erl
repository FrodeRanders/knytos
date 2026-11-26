%% SPDX-License-Identifier: Apache-2.0
-module(knytos).
-export([call/2, call/3, request/3, request/4, cancel/1, lookup/1, close/1,
         subscribe/2, unsubscribe/1, log/2]).

call(Service, Request) ->
    call(Service, Request, infinity).

call(Service, Request, Timeout)
  when Timeout =:= infinity; is_integer(Timeout), Timeout >= 0, Timeout =< 16#FFFFFFFF ->
    case Request of
        {Opcode, Argument} ->
            await(?MODULE:request(Service, Opcode, Argument), Timeout);
        {Opcode, Argument, Bytes} ->
            await(?MODULE:request(Service, Opcode, Argument, Bytes), Timeout);
        _ -> erlang:error(badarg)
    end;
call(_, _, _) ->
    erlang:error(badarg).

await({ok, Ref}, Timeout) ->
    receive {knytos_reply, Ref, Result} -> Result
    after Timeout ->
        %% A delivered reply can race the timeout. Once cancel returns,
        %% this reference can only have a reply already in this mailbox.
        case ?MODULE:cancel(Ref) of
            ok -> ok;
            {error, not_found} ->
                receive {knytos_reply, Ref, _} -> ok after 0 -> ok end
        end,
        {error, timeout}
    end;
await({error, _} = Error, _) ->
    Error.

%% External calls resolve to the platform's statically registered NIFs.
request(_, _, _) -> erlang:nif_error(undefined).
request(_, _, _, _) -> erlang:nif_error(undefined).
cancel(_) -> erlang:nif_error(undefined).
lookup(_) -> erlang:nif_error(undefined).
close(_) -> erlang:nif_error(undefined).
subscribe(_, _) -> erlang:nif_error(undefined).
unsubscribe(_) -> erlang:nif_error(undefined).
log(_, _) -> erlang:nif_error(undefined).
