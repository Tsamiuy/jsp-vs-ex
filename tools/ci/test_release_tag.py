#!/usr/bin/env python3
"""Exercise the actual release tag step with a fake API; no network or tag writes.

Run: python3 tools/ci/test_release_tag.py
"""
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SHA = 'a' * 40
OTHER = 'b' * 40
FAKE_GH = '''#!/usr/bin/env python3
import json, os, pathlib, sys
state_path = pathlib.Path(os.environ['FAKE_STATE'])
s = json.loads(state_path.read_text())
a = sys.argv[1:]
endpoint = next(x for x in a if x.startswith('repos/'))
if s.get('error'):
    sys.exit(1)
if '-X' in a and a[a.index('-X')+1] == 'POST' or '--method' in a and a[a.index('--method')+1] == 'POST':
    s['writes'] = s.get('writes', 0) + 1
    s['tag'] = {'ref':'refs/tags/v0.1.1', 'object':{'type':'commit', 'sha':os.environ['GITHUB_SHA']}}
    state_path.write_text(json.dumps(s))
    print(json.dumps(s['tag']))
elif '/git/matching-refs/' in endpoint:
    print(json.dumps(([s['tag']] if s.get('tag') else []) + s.get('prefix_refs', [])))
elif '/releases?' in endpoint:
    print(json.dumps(s.get('release_pages', [s.get('releases', [])])))
elif '/git/tags/' in endpoint:
    print(json.dumps(s['annotated']))
elif '/git/ref/tags/' in endpoint:
    if not s.get('tag'): sys.exit(1)
    print(json.dumps(s['tag']))
else:
    sys.exit('unexpected fake API endpoint: '+endpoint)
'''


def tag(sha=SHA, kind='commit'):
    return {'ref': 'refs/tags/v0.1.1', 'object': {'sha': sha, 'type': kind}}


class ReleaseStepTests(unittest.TestCase):
    def run_step(self, state):
        # Execute the workflow's shell itself, including output propagation.
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        step = workflow.split('      - name: Create the tag unless it exists\n', 1)[1]
        script = textwrap.dedent(step.split('        run: |\n', 1)[1].split('\n  build:', 1)[0])
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'bin').mkdir()
            (root / 'tools/ci').mkdir(parents=True)
            (root / 'installer').mkdir()
            (root / 'installer/VERSION').write_text('0.1.1\n')
            (root / 'CHANGELOG.md').write_text('# Changelog\n\n## [0.1.1]\n\n- Test release.\n')
            shutil.copy2(ROOT / 'tools/release.py', root / 'tools/release.py')
            helper = ROOT / 'tools/ci/release_tag.py'
            if helper.exists():
                shutil.copy2(helper, root / 'tools/ci/release_tag.py')
            cli = root / 'bin/gh'
            cli.write_text(FAKE_GH)
            cli.chmod(0o755)
            state_file = root / 'state.json'
            state_file.write_text(json.dumps(state))
            output = root / 'outputs'
            env = dict(os.environ, PATH=str(root / 'bin') + os.pathsep + os.environ['PATH'],
                       GITHUB_SHA=SHA, GITHUB_REPOSITORY='owner/repo', GITHUB_OUTPUT=str(output),
                       FAKE_STATE=str(state_file))
            proc = subprocess.run(['bash', '-c', script], cwd=root, env=env,
                                  capture_output=True, text=True, timeout=10, check=False)
            outputs = dict(line.split('=', 1) for line in output.read_text().splitlines()) if output.exists() else {}
            return proc, outputs, json.loads(state_file.read_text())

    def test_retry_after_build_failure(self):
        proc, out, state = self.run_step({'tag': tag()})
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(out.get('build'), 'true', 'an existing tag without a release must resume the build')
        self.assertEqual(state.get('writes', 0), 0, 'never move or recreate the existing tag')

    def test_new_version_creates_one_tag(self):
        proc, out, state = self.run_step({})
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(out.get('build'), 'true')
        self.assertEqual(out.get('version'), '0.1.1')
        self.assertEqual(state.get('writes'), 1)

    def test_other_commit_refused(self):
        proc, out, state = self.run_step({'tag': tag(OTHER)})
        self.assertNotEqual(proc.returncode, 0)
        self.assertNotEqual(out.get('build'), 'true')
        self.assertEqual(state.get('writes', 0), 0)

    def test_published_or_draft_release_skipped(self):
        for draft in (False, True):
            with self.subTest(draft=draft):
                proc, out, state = self.run_step({'tag': tag(), 'releases': [{'tag_name': 'v0.1.1', 'draft': draft}]})
                self.assertEqual(proc.returncode, 0, proc.stderr)
                self.assertEqual(out.get('build'), 'false')
                self.assertEqual(state.get('writes', 0), 0)

    def test_existing_release_on_previous_main_skipped(self):
        proc, out, state = self.run_step({'tag': tag(OTHER), 'releases': [{'tag_name': 'v0.1.1'}]})
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(out.get('build'), 'false')
        self.assertEqual(state.get('writes', 0), 0)

    def test_read_failure_never_creates_tag(self):
        proc, out, state = self.run_step({'error': True})
        self.assertNotEqual(proc.returncode, 0)
        self.assertNotEqual(out.get('build'), 'true')
        self.assertEqual(state.get('writes', 0), 0)

    def test_similar_tag_does_not_match(self):
        ref = tag(OTHER)
        ref['ref'] += '0'
        proc, out, state = self.run_step({'prefix_refs': [ref]})
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(out.get('build'), 'true')
        self.assertEqual(state.get('writes'), 1)

    def test_annotated_tag_same_commit_resumes(self):
        proc, out, state = self.run_step({'tag': tag(OTHER, 'tag'), 'annotated': {'object': {'type': 'commit', 'sha': SHA}}})
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(out.get('build'), 'true')
        self.assertEqual(state.get('writes', 0), 0)

    def test_release_on_later_page_skipped(self):
        proc, out, state = self.run_step({'tag': tag(), 'release_pages': [
            [{'tag_name': 'v0.1.0'}], [{'tag_name': 'v0.1.1', 'draft': True}]]})
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(out.get('build'), 'false')
        self.assertEqual(state.get('writes', 0), 0)

    def test_broken_annotation_refused(self):
        proc, out, state = self.run_step({'tag': tag(OTHER, 'tag'), 'annotated': {'object': {'type': 'tag', 'sha': OTHER}}})
        self.assertNotEqual(proc.returncode, 0)
        self.assertNotEqual(out.get('build'), 'true')
        self.assertEqual(state.get('writes', 0), 0)


class PreviewTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('release_tag', ROOT / 'tools/ci/release_tag.py')
        self.helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.helper)

    def test_default_preview_never_creates_tag(self):
        with mock.patch.object(self.helper, 'api', side_effect=[[], []]) as api:
            build, message = self.helper.prepare('owner/repo', '0.1.1', SHA)
        self.assertTrue(build)
        self.assertIn('Would create', message)
        self.assertEqual(api.call_count, 2)
        self.assertTrue(all('fields' not in call.kwargs for call in api.call_args_list))

    def test_invalid_identity_never_contacts_api(self):
        for repo, version, sha in (('owner/repo/extra', '0.1.1', SHA),
                                   ('owner/repo', '01.1.1', SHA),
                                   ('owner/repo', '0.1.1', 'main')):
            with self.subTest(repo=repo, version=version, sha=sha), mock.patch.object(self.helper, 'api') as api:
                with self.assertRaises(ValueError):
                    self.helper.prepare(repo, version, sha, True)
                api.assert_not_called()


if __name__ == '__main__':
    unittest.main()
