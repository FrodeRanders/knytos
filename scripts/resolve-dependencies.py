#!/usr/bin/env python3
"""Materialize the pinned Sitas tree without requiring a sibling checkout.

No network is used unless --fetch is supplied. A local Git source is read at
the lock's commit, independently of its checkout/branch or uncommitted changes.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import tomllib

ROOT = Path(__file__).resolve().parents[1]
RECEIPT = '.knytos-source.json'


def read_pin(root):
    lock = tomllib.loads((root / 'dependencies.lock').read_text())
    pin = lock['sitas']
    if lock['schema'] != 1 or not re.fullmatch('[0-9a-f]{40}', pin['revision']):
        raise ValueError('dependencies.lock requires schema 1 and a full Sitas commit')
    return pin


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file() and p.name != RECEIPT}


def verify(root, pin):
    receipt = json.loads((root / RECEIPT).read_text())
    if receipt['pin'] != pin:
        raise ValueError('cached Sitas revision differs from dependencies.lock')
    for name, digest in receipt['files'].items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError(f'unsafe cached Sitas path: {name}')
        path = root / name
        if not path.is_file() or path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'cached Sitas input differs: {name}')
    present = {str(p.relative_to(root)) for p in root.rglob('*') if p.is_file()}
    if present != set(receipt['files']) | {RECEIPT}:
        raise ValueError('cached Sitas contains unlisted inputs')
    return root


def materialize(root, destination, source=None, fetch=False):
    pin = read_pin(root)
    destination = destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Serialize publishers, including separate SDK exports sharing a cache.
    with (destination.parent / ('.' + destination.name + '.lock')).open('w') as guard:
        fcntl.flock(guard, fcntl.LOCK_EX)
        if destination.exists() and source is None:
            try:
                return verify(destination, pin)
            except (OSError, ValueError, KeyError):
                if not fetch:
                    raise
        cache = root / '.dependencies/sitas'
        with tempfile.TemporaryDirectory(prefix='sitas-', dir=destination.parent) as temporary:
            temporary = Path(temporary)
            tree = temporary / 'tree'
            tree.mkdir()
            cached = False
            if source is None and cache.exists() and cache.resolve() != destination:
                try:
                    verify(cache, pin)
                    cached = True
                except (OSError, ValueError, KeyError):
                    if not fetch:
                        raise
            if cached:
                shutil.copytree(cache, tree, dirs_exist_ok=True)
            else:
                if source is None:
                    if not fetch:
                        raise ValueError('Sitas is unresolved; run scripts/resolve-dependencies.py '
                                         '--sitas-source /path/to/sitas (offline), or --fetch')
                    source = temporary / 'git'
                    subprocess.run(['git', 'init', '--quiet', str(source)], check=True)
                    subprocess.run(['git', '-C', str(source), 'fetch', '--depth=1',
                                    pin['repository'], pin['revision']], check=True)
                source = Path(source).resolve()
                commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse',
                                                  pin['revision'] + '^{commit}'], text=True).strip()
                if commit != pin['revision']:
                    raise ValueError('Sitas source does not contain the locked commit')
                archive = temporary / 'source.tar'
                with archive.open('wb') as output:
                    subprocess.run(['git', '-C', str(source), 'archive', commit], stdout=output, check=True)
                with tarfile.open(archive) as bundle:
                    # Sitas has no source symlinks. Reject links and traversal.
                    for member in bundle.getmembers():
                        path = Path(member.name)
                        if path.is_absolute() or '..' in path.parts or not (member.isfile() or member.isdir()):
                            raise ValueError(f'unsupported Sitas archive entry: {member.name}')
                    bundle.extractall(tree, filter='data')
                for name in ('sitas-core', 'sitas-knytos', 'sitas-unix'):
                    if not (tree / 'crates' / name / 'Cargo.toml').is_file():
                        raise ValueError(f'missing Sitas crate: {name}')
                # cargo fmt --all traverses path dependencies. Do not let the
                # enclosing OS's rustfmt settings rewrite a third-party input.
                if not (tree / 'rustfmt.toml').exists() and not (tree / '.rustfmt.toml').exists():
                    (tree / 'rustfmt.toml').write_text('# Generated formatting boundary for pinned sources.\ndisable_all_formatting = true\n')
                (tree / RECEIPT).write_text(json.dumps({'pin': pin, 'files': hashes(tree)},
                                                     indent=2, sort_keys=True) + '\n')
            verify(tree, pin)
            # Keep the prior usable snapshot until preparation has succeeded.
            previous = temporary / 'previous'
            if destination.exists():
                destination.rename(previous)
            try:
                tree.rename(destination)
            except OSError:
                if previous.exists():
                    previous.rename(destination)
                raise
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sitas-source', type=Path, default=os.environ.get('KNYTOS_SITAS_DIR') or None)
    parser.add_argument('--destination', type=Path, default=ROOT / '.dependencies/sitas')
    parser.add_argument('--fetch', action='store_true', help='allow fetching the locked commit')
    parser.add_argument('--check', action='store_true', help='only verify the existing snapshot')
    args = parser.parse_args()
    if args.check:
        result = verify(args.destination, read_pin(ROOT))
    else:
        result = materialize(ROOT, args.destination, args.sitas_source, args.fetch)
    print(result)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f'error: {error}', file=sys.stderr)
        sys.exit(1)
