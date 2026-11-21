#!/usr/bin/env python3
"""Exercise source pins and failure-safe replacement without a network."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('dependencies', ROOT / 'scripts/resolve-dependencies.py')
dependencies = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dependencies)


class DependencyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='dependency tests ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'arbitrary source'
        self.source.mkdir()
        self.git('init', '--quiet')
        self.git('config', 'user.email', 'fixture@example.invalid')
        self.git('config', 'user.name', 'Fixture')
        for name in ('sitas-core', 'sitas-knytos', 'sitas-unix'):
            path = self.source / 'crates' / name
            path.mkdir(parents=True)
            (path / 'Cargo.toml').write_text(f'[package]\nname="{name}"\n')
        self.input = self.source / 'crates/sitas-core/input.rs'
        self.input.write_text('locked bytes')
        self.git('add', '.')
        self.git('commit', '--quiet', '-m', 'locked')
        self.revision = self.git('rev-parse', 'HEAD').strip()
        self.pin = {'repository': str(self.source), 'revision': self.revision}
        self.write_pin()
        self.destination = self.root / 'consumer/.dependencies/sitas'

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.source), *args], text=True)

    def write_pin(self):
        (self.root / 'dependencies.lock').write_text(
            f'schema=1\n[sitas]\nrepository="{self.pin["repository"]}"\nrevision="{self.pin["revision"]}"\n')

    def test_offline_source_uses_commit_and_does_not_modify_checkout(self):
        self.input.write_text('later commit')
        self.git('add', '.')
        self.git('commit', '--quiet', '-m', 'later')
        self.input.write_text('uncommitted')
        before = self.git('status', '--porcelain')
        dependencies.materialize(self.root, self.destination, self.source)
        self.assertEqual((self.destination / 'crates/sitas-core/input.rs').read_text(), 'locked bytes')
        self.assertEqual(self.git('status', '--porcelain'), before)
        self.assertEqual(self.input.read_text(), 'uncommitted')
        dependencies.verify(self.destination, self.pin)

    def test_missing_source_has_explicit_network_opt_in(self):
        with self.assertRaisesRegex(ValueError, '--fetch'):
            dependencies.materialize(self.root, self.destination)
        self.assertFalse(self.destination.exists())

    def test_fetch_and_pin_change_with_local_git_remote(self):
        dependencies.materialize(self.root, self.destination, fetch=True)
        self.input.write_text('new pin')
        self.git('add', '.')
        self.git('commit', '--quiet', '-m', 'new pin')
        self.pin['revision'] = self.git('rev-parse', 'HEAD').strip()
        self.write_pin()
        with self.assertRaisesRegex(ValueError, 'revision differs'):
            dependencies.materialize(self.root, self.destination)
        dependencies.materialize(self.root, self.destination, fetch=True)
        self.assertEqual((self.destination / 'crates/sitas-core/input.rs').read_text(), 'new pin')

    def test_failed_replacement_preserves_previous_tree_and_tampering_rejects(self):
        dependencies.materialize(self.root, self.destination, self.source)
        self.pin['revision'] = '0' * 40
        self.write_pin()
        with self.assertRaises(subprocess.CalledProcessError):
            dependencies.materialize(self.root, self.destination, self.source)
        self.assertEqual((self.destination / 'crates/sitas-core/input.rs').read_text(), 'locked bytes')
        self.pin['revision'] = self.revision
        (self.destination / 'crates/sitas-core/input.rs').write_text('tampered')
        with self.assertRaisesRegex(ValueError, 'input differs'):
            dependencies.verify(self.destination, self.pin)

    def test_export_from_unrelated_directory_has_closed_source_paths(self):
        # Use the real pinned cache; this exercises the public exporter itself.
        archive = self.root / 'SDK with spaces.tar.gz'
        env = os.environ.copy()
        env.pop('KNYTOS_SITAS_DIR', None)
        subprocess.run([str(ROOT / 'scripts/export-app-sdk.sh'), '--output', str(archive)],
                       cwd=self.root, env=env, check=True, capture_output=True)
        import json
        import tarfile
        with tarfile.open(archive) as bundle:
            metadata = json.load(bundle.extractfile('knytos-app-sdk/sdk.json'))
            self.assertEqual(metadata['sitas_revision'], dependencies.read_pin(ROOT)['revision'])
            for member in bundle.getmembers():
                if member.name.endswith('/Cargo.toml'):
                    text = bundle.extractfile(member).read().decode()
                    self.assertNotIn('gautelis', text)
                    self.assertNotIn('../../.dependencies/', text)


if __name__ == '__main__':
    unittest.main()
