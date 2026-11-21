# Repository dependencies and SDK selection

The dependency direction is Sitas → Knytos SDK → application. Sitas does not
need a Knytos checkout to build. Knytos selects one Sitas commit in
`dependencies.lock`. Applications consume one exported SDK source closure,
including its Sitas backends, rather than combining independent Git dependencies.
`Cargo.lock` continues to pin registry packages in each workspace.

## Prepare Knytos

From a Knytos checkout at any location:

```sh
python3 scripts/resolve-dependencies.py --fetch
# Or resolve without contacting GitHub:
python3 scripts/resolve-dependencies.py --sitas-source /any/path/to/sitas
python3 scripts/resolve-dependencies.py --check
cargo fmt --all -- --check
scripts/build-knytos-services.sh
```

The resolver reads a local Git source at the exact locked commit. Its branch,
HEAD and uncommitted files do not change the selected input. It never checks out
or writes into that source repository. A missing commit fails explicitly; there
is no fallback to HEAD. `KNYTOS_SITAS_DIR` is the environment spelling of
`--sitas-source`. To test Sitas changes, commit them locally and update the pin;
publication is unnecessary for this offline workflow.

Cargo paths refer only to the generated `.dependencies/sitas` tree. Preparation
records file hashes and supplies a formatting boundary so Knytos's rustfmt
configuration cannot rewrite the third-party snapshot. Repeated resolution
verifies the selected inputs. After changing the lock, pass `--fetch` or the local
source again to replace the snapshot. A failed preparation retains the previous
tree. These are consistency checks, not signed dependency provenance.

## Export and install

```sh
KNYTOS_SITAS_DIR=/any/path/to/sitas scripts/export-app-sdk.sh --output /tmp/sdk.tar.gz
# Or use an already prepared cache; --fetch explicitly allows a missing input
# to be downloaded. Export works from an unrelated working directory too.
scripts/export-app-sdk.sh --fetch --output /tmp/sdk.tar.gz
```

Export reads the OS checkout's lock and toolchain, includes local OS source
changes, and records its commit, dirty state, runtime ABI and exact file hashes
in `sdk.json`. Sitas always comes from its locked commit. The resulting archive
has no paths to either source checkout. SDK-created applications copy the SDK
under their own `.knytos/` and use relative Cargo and plan paths.

The broker supports both strict installation and explicit replacement:

```sh
tools/knytos-sdk.sh unpack /tmp/sdk.tar.gz
# Replace stale knytos.lock/toolchain selection with this SDK's actual metadata:
tools/knytos-sdk.sh unpack /tmp/sdk.tar.gz --force
# Or export and force adopt from a local OS checkout:
KNYTOS_SITAS_DIR=/any/path/to/sitas tools/knytos-sdk.sh use-os /any/path/to/knytos --force
tools/knytos-sdk.sh verify
cargo test
```

Without `--force`, installation must match `knytos.lock`. Force adoption updates
the complete OS/Sitas/schema/toolchain tuple, the Rust toolchain channel and an
exact SDK manifest fingerprint. It verifies content before changing selection;
it does not disable integrity checks. Review the changed pins and run application
checks. If platform registry dependencies changed, update the application's
`Cargo.lock` through Cargo as part of that review.

Local SDK development does not require a GitHub round trip. An SDK exported
from uncommitted OS changes cannot be recreated from a Git commit alone. Keep
its archive for exact local reproduction. To make an application pin usable by
GitHub CI, commit/publish the OS changes, export from that clean revision, and
adopt the published SDK. Source-only fetching intentionally rejects a different
snapshot when `SDK_MANIFEST_SHA256` is pinned.

## CI and QEMU

Knytos CI calls the same resolver with `--fetch` in each job needing workspace
sources. No workflow duplicates the Sitas revision or constructs a developer
directory hierarchy. SDK portability tests export from another working directory
and inspect all packaged Cargo paths. The SDK job also moves a generated project
before its host tests and both target builds.

Sitas CI lives at `.github/workflows/ci.yml` and builds Sitas independently.
The broker's `fetch` command uses application-owned
`.knytos/sources/{knytos,sitas}` clones, passing the Sitas path explicitly.
Its QEMU runner similarly prepares the selected OS's dependency cache inside
application-owned runner state. A clean SDK uses the pinned OS commit. A local
dirty SDK can instead use a snapshot of the explicitly supplied OS worktree;
the runner verifies that it still exports the selected SDK fingerprint before
copying tracked and untracked, non-ignored sources into its own clone. Thus local
SDK and runner changes need no GitHub publication, while Git-only CI still needs
published commits. The original OS/Sitas inputs remain read-only.
