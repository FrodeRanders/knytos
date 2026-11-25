//! Signed scalar service used by the AtomVM cancellation fixture.
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
        Endpoint,
        IpcError,
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
    Serve(ServeError<IpcError>),
}

pub fn run(ctx: &Context) -> Result<AppExit, Error> {
    let app = Application::from_context(ctx).map_err(Error::Launch)?;
    let endpoint = Endpoint::create(knytos_app::name(b"M4HOLD"), 1, 8).map_err(Error::Ipc)?;
    let mut server = Server::new(ctx, endpoint, 16).map_err(|(_, error)| Error::Ipc(error))?;
    server.publish(&app, b"m4.hold", &Deadline::after(5_000)).map_err(Error::Grant)?;
    logln!("[m4 hold] published");

    let mut held: Option<ReplyToken> = None;
    let shutdown = server
        .serve(|mut message| {
            let Some(reply) = message.reply.take() else {
                return Ok(());
            };
            let result = match message.opcode {
                1 if held.is_none() => {
                    held = Some(reply);
                    logln!("[m4 hold] accepted case={}", message.arg0);
                    return Ok(());
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
            reply.reply(result)
        })
        .map_err(Error::Serve)?;
    Ok(AppExit::Shutdown(shutdown))
}

#[cfg(not(test))]
knytos_app::entry!(run);
