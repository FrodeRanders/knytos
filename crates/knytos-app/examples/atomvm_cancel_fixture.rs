//! Signed service used by the AtomVM service-worker fixtures.
#![cfg_attr(not(test), no_std)]
#![cfg_attr(not(test), no_main)]

use knytos_app::{
    AppExit,
    Application,
    Context,
    LaunchError,
    deadline::Deadline,
    grant_client,
    logln,
    owned::{
        Completion,
        CompletionError,
        Endpoint,
        IpcError,
        OwnedMemory,
        ReplyToken,
    },
    service::{
        ServeError,
        Server,
    },
};

#[derive(Debug)]
pub enum Error {
    Launch(LaunchError),
    Grant(grant_client::Error),
    Ipc(IpcError),
    Serve(ServeError<HandlerError>),
    Delay(CompletionError),
}

#[derive(Debug)]
pub enum HandlerError {
    Ipc(IpcError),
    CloseEndpoint,
}

impl From<IpcError> for HandlerError {
    fn from(error: IpcError) -> Self {
        Self::Ipc(error)
    }
}

pub fn run(ctx: &Context) -> Result<AppExit, Error> {
    let app = Application::from_context(ctx).map_err(Error::Launch)?;
    let endpoint = Endpoint::create(knytos_app::name(b"M4HOLD"), 1, 8).map_err(Error::Ipc)?;
    let mut server = Server::new(ctx, endpoint, 16).map_err(|(_, error)| Error::Ipc(error))?;
    server.publish(&app, b"m4.hold", &Deadline::after(5_000)).map_err(Error::Grant)?;
    logln!("[m4 hold] published");

    let mut held: Option<ReplyToken> = None;
    let result = server.serve(|mut message| {
        let Some(reply) = message.reply.take() else {
            return Ok(());
        };
        let result = match message.opcode {
            1 if held.is_none() => {
                held = Some(reply);
                logln!(
                    "[m4 hold] accepted case={} payload={}",
                    message.arg0,
                    message.memory.is_some()
                );
                return Ok(());
            }
            // Return the moved page and let the client validate the
            // initialized byte count, including deliberately bad counts.
            3 => match message.memory.take() {
                Some(memory) => {
                    return reply
                        .reply_move(memory, message.arg0 as i64)
                        .map_err(|(_, error)| error.into());
                }
                None => -1,
            },
            4 => {
                let memory = OwnedMemory::allocate(2).map_err(|_| IpcError::CreationFailed)?;
                return reply.reply_move(memory, 1).map_err(|(_, error)| error.into());
            }
            5 => {
                reply.reply(0)?;
                return Err(HandlerError::CloseEndpoint);
            }
            2 => {
                if held.take().is_some_and(|previous| previous.reply(1).is_err()) {
                    logln!("[m4 hold] late reply rejected case={}", message.arg0);
                    42
                } else {
                    -1
                }
            }
            _ => -1,
        };
        reply.reply(result)?;
        Ok(())
    });
    match result {
        Ok(shutdown) => Ok(AppExit::Shutdown(shutdown)),
        Err(ServeError::Handler(HandlerError::CloseEndpoint)) => {
            // Give the client the scalar acknowledgement first. Its next
            // wait has only the endpoint-close completion as a producer.
            Completion::timer(100).and_then(Completion::wait).map_err(Error::Delay)?;
            logln!("[m4 hold] closing endpoint");
            drop(server);
            Ok(AppExit::Completed)
        }
        Err(error) => Err(Error::Serve(error)),
    }
}

#[cfg(not(test))]
knytos_app::entry!(run);
