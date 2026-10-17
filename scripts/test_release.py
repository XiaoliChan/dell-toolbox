"""Local bare-remote tests: no GitHub credentials, network, or actual release writes."""
import contextlib
import io
import json
import os
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.old = Path.cwd()
        root = Path(self.temp.name)
        self.remote = root / 'remote.git'
        release.run('git', 'init', '--bare', str(self.remote))
        self.repo = root / 'repo'
        self.repo.mkdir()
        os.chdir(self.repo)
        release.git('init', '-b', 'main')
        release.git('config', 'user.name', 'Test')
        release.git('config', 'user.email', 'test@example.invalid')
        for name in release.VERSION_FILES:
            path = Path(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text((self.old / name).read_text(encoding='utf-8'), encoding='utf-8')
        release.set_version('1.2.3')
        Path('code.txt').write_text('base\n', encoding='utf-8')
        release.git('add', '.')
        release.git('commit', '-m', 'baseline')
        release.git('tag', 'v1.2.3')
        Path('code.txt').write_text('fix\n', encoding='utf-8')
        release.git('commit', '-am', 'Fix a regression')
        self.base = release.git('rev-parse', 'HEAD')
        release.git('remote', 'add', 'origin', str(self.remote))
        release.git('push', 'origin', 'main', '--tags')
        self.output_env = patch.dict(os.environ, {'GITHUB_OUTPUT': ''})
        self.output_env.start()

    def tearDown(self):
        self.output_env.stop()
        os.chdir(self.old)
        self.temp.cleanup()

    def prepare(self, publish=True, ref='refs/heads/main'):
        with contextlib.redirect_stdout(io.StringIO()):
            return release.prepare('push', ref, publish)

    def finish(self):
        with contextlib.redirect_stdout(io.StringIO()):
            return release.finalize()

    def advance_remote(self):
        # Use a separate clone to model a developer push while builds run.
        other = Path(self.temp.name) / 'other'
        release.run('git', 'clone', '-b', 'main', str(self.remote), str(other))
        release.run('git', '-C', str(other), 'config', 'user.name', 'Other')
        release.run('git', '-C', str(other), 'config', 'user.email', 'other@example.invalid')
        (other / 'other.txt').write_text('new commit', encoding='utf-8')
        release.run('git', '-C', str(other), 'add', '.')
        release.run('git', '-C', str(other), 'commit', '-m', 'Newer change')
        release.run('git', '-C', str(other), 'push', 'origin', 'main')

    def test_prepare_only_changes_local_version_and_changelog(self):
        plan = self.prepare()
        self.assertEqual(plan['version'], '1.2.4')
        self.assertEqual(release.version(), '1.2.4')
        self.assertEqual(release.remote_refs()['refs/heads/main'], self.base)
        self.assertIsNone(release.tag_commit(release.remote_refs(), 'v1.2.4'))
        self.assertIn('Fix a regression', Path('CHANGELOG.md').read_text(encoding='utf-8'))
        self.assertEqual(release.notes_from_changelog('1.2.4'), plan['notes'])

    def test_manual_validation_does_not_modify_version(self):
        plan = self.prepare(publish=False)
        self.assertEqual(plan['mode'], 'validate')
        self.assertEqual(release.version(), '1.2.3')
        self.assertEqual(release.git('status', '--porcelain'), '')
        with self.assertRaisesRegex(RuntimeError, 'cannot publish'):
            self.finish()

    def test_finalize_atomic_and_rerun_reuses_same_commit(self):
        self.prepare()
        published = self.finish()
        sha = published['release_sha']
        refs = release.remote_refs()
        self.assertEqual(refs['refs/heads/main'], sha)
        self.assertEqual(release.tag_commit(refs, 'v1.2.4'), sha)
        release.git('checkout', '--detach', self.base)
        rerun = self.prepare()
        self.assertEqual(rerun['mode'], 'reuse')
        self.assertEqual(release.git('rev-parse', 'HEAD'), sha)
        self.assertEqual(self.finish()['release_sha'], sha)
        self.assertEqual(release.version(), '1.2.4')

    def test_dispatch_on_release_commit_does_not_bump_again(self):
        self.prepare()
        sha = self.finish()['release_sha']
        plan = self.prepare()
        self.assertEqual(plan['mode'], 'reuse')
        self.assertEqual(self.finish()['release_sha'], sha)

    def test_main_advance_during_build_prevents_commit_and_tag(self):
        self.prepare()
        self.advance_remote()
        with self.assertRaisesRegex(RuntimeError, 'advanced during build'):
            self.finish()
        self.assertIsNone(release.tag_commit(release.remote_refs(), 'v1.2.4'))
        self.assertEqual(release.git('rev-parse', 'HEAD'), self.base)

    def test_stale_trigger_fails_before_version_changes(self):
        self.advance_remote()
        with self.assertRaisesRegex(RuntimeError, 'advanced'):
            self.prepare()
        self.assertEqual(release.version(), '1.2.3')

    def test_unstaged_code_mutation_rejected(self):
        self.prepare()
        Path('code.txt').write_text('not built', encoding='utf-8')
        with self.assertRaisesRegex(RuntimeError, 'source changed'):
            self.finish()

    def test_staged_code_mutation_rejected(self):
        self.prepare()
        Path('code.txt').write_text('not built', encoding='utf-8')
        release.git('add', 'code.txt')
        with self.assertRaisesRegex(RuntimeError, 'tree changed'):
            self.finish()

    def test_version_drift_rejected(self):
        Path('CMakeLists.txt').write_text('project(test VERSION 9.9.9)', encoding='utf-8')
        with self.assertRaisesRegex(RuntimeError, 'drift'):
            release.version()

    def test_tag_collision_rejected(self):
        release.git('tag', 'v1.2.4')
        release.git('push', 'origin', 'v1.2.4')
        with self.assertRaisesRegex(RuntimeError, 'already exists'):
            self.prepare()

    def test_annotated_tag_peels_to_commit(self):
        release.git('tag', '-a', 'v1.2.3-review', '-m', 'Annotated test')
        release.git('push', 'origin', 'v1.2.3-review')
        self.assertEqual(release.tag_commit(release.remote_refs(), 'v1.2.3-review'), self.base)

    def test_external_tag_build_preserves_version(self):
        release.git('checkout', '--detach', 'v1.2.3')
        tagged = release.git('rev-parse', 'HEAD')
        plan = self.prepare(ref='refs/tags/v1.2.3')
        self.assertEqual(plan['mode'], 'tag')
        self.assertEqual(self.finish()['release_sha'], tagged)

    def test_patch_overflow_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'overflow'):
            release.bump('1.2.65535')

    def test_publish_requires_finalize(self):
        self.prepare()
        with self.assertRaisesRegex(RuntimeError, 'Finalize'):
            release.publish_release()

    def publish_fixture(self):
        self.prepare()
        self.finish()
        Path('dell-toolbox-windows-x64.zip').write_bytes(b'zip fixture')
        Path('dist').mkdir()
        Path('dist/DellToolbox-1.2.4-setup.exe').write_bytes(b'installer fixture')

    def test_publish_new_and_existing_release(self):
        self.publish_fixture()
        real_run = release.run
        for existing in (False, True):
            calls = []
            def fake(*args, **kwargs):
                if args[0] != 'gh':
                    return real_run(*args, **kwargs)
                calls.append(args)
                payload = [[{'tag_name': 'v1.2.4'}]] if existing else [[]]
                return subprocess.CompletedProcess(args, 0, json.dumps(payload), '')
            with patch.object(release, 'run', side_effect=fake):
                release.publish_release()
            operations = [call[2] for call in calls if call[1] == 'release']
            self.assertEqual(operations, ['edit', 'upload'] if existing else ['create'])
            self.assertTrue(any('--notes-file' in call for call in calls))
            self.assertEqual(release.NOTES.read_text(encoding='utf-8'), release.notes_from_changelog('1.2.4'))

    def test_publish_api_failure_is_not_treated_as_missing_release(self):
        self.publish_fixture()
        real_run = release.run
        def fake(*args, **kwargs):
            if args[0] == 'gh':
                raise RuntimeError('API unavailable')
            return real_run(*args, **kwargs)
        with patch.object(release, 'run', side_effect=fake):
            with self.assertRaisesRegex(RuntimeError, 'API unavailable'):
                release.publish_release()

    def test_process_failure_reports_stderr(self):
        with self.assertRaisesRegex(RuntimeError, 'fixture stderr'):
            release.run(sys.executable, '-c', 'import sys; sys.stderr.write("fixture stderr"); sys.exit(3)')


if __name__ == '__main__':
    unittest.main()
