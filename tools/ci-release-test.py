#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Release gates and secret isolation with deterministic fake tools/APIs."""
import base64
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('ci_release', Path(__file__).with_name('ci-release.py'))
R = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(R)
COMMIT = 'a' * 40


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'app').mkdir()
        (self.root / 'app/build.gradle').write_text('versionName "1.6.5"\nversionCode 77\n')
        (self.root / 'docs').mkdir()
        (self.root / 'docs/release-notes-1.6.5.md').write_text('Release notes\n')
        self.state = {'commit': COMMIT, 'tag': 'v1.6.5', 'version': '1.6.5',
                      'version_code': 77, 'source': str(self.root), 'ci_run_id': 12}
        self.green = {'workflow_runs': [{'head_sha': COMMIT, 'status': 'completed',
                                        'conclusion': 'success', 'id': 12}]}

    def test_ref_validation_rejects_shell_and_nonstable_inputs(self):
        R.validate_ref(COMMIT, 'v1.6.5')
        for commit, tag in [('main', 'v1.6.5'), (COMMIT, 'v1.6.5;echo hi'),
                            (COMMIT, 'v1.6.5-beta'), (COMMIT, 'v1.6.5\n'),
                            ('A'*40, 'v1.6.5'), ('-'*40, 'v1.6.5')]:
            with self.subTest(commit=commit, tag=tag), self.assertRaises(R.ReleaseError):
                R.validate_ref(commit, tag)

    def test_prepare_records_exact_successful_source(self):
        with patch.dict(os.environ, GITHUB_REPOSITORY=R.REPOSITORY), \
             patch.object(R, 'run', side_effect=[COMMIT, '']), patch.object(R, 'api', return_value=self.green):
            path = self.root / 'state.json'
            value = R.prepare(self.root, COMMIT, 'v1.6.5', path)
            self.assertEqual(json.loads(path.read_text()), value)
            self.assertEqual(value['ci_run_id'], 12)

    def test_prepare_rejects_unchecked_or_failed_ci(self):
        for runs in [[], [{'head_sha': COMMIT, 'status': 'in_progress', 'conclusion': None}],
                     [{'head_sha': COMMIT, 'status': 'completed', 'conclusion': 'failure'}],
                     [{'head_sha': 'b'*40, 'status': 'completed', 'conclusion': 'success'}]]:
            with self.subTest(runs=runs), patch.dict(os.environ, GITHUB_REPOSITORY=R.REPOSITORY), \
                 patch.object(R, 'run', side_effect=[COMMIT, '']), \
                 patch.object(R, 'api', return_value={'workflow_runs': runs}), self.assertRaises(R.ReleaseError):
                R.prepare(self.root, COMMIT, 'v1.6.5', self.root / 'state.json')

    def test_prepare_rejects_checkout_owner_and_ancestry_mismatch(self):
        with patch.dict(os.environ, GITHUB_REPOSITORY='other/repo'), self.assertRaises(R.ReleaseError):
            R.prepare(self.root, COMMIT, 'v1.6.5', self.root / 'state.json')
        for side in [['b'*40], [COMMIT, R.ReleaseError('not ancestor')]]:
            with patch.dict(os.environ, GITHUB_REPOSITORY=R.REPOSITORY), patch.object(R, 'run', side_effect=side), \
                 self.assertRaises(R.ReleaseError):
                R.prepare(self.root, COMMIT, 'v1.6.5', self.root / 'state.json')

    def test_version_tag_and_missing_notes(self):
        with patch.dict(os.environ, GITHUB_REPOSITORY=R.REPOSITORY), \
             patch.object(R, 'run', side_effect=[COMMIT, '']), self.assertRaises(R.ReleaseError):
            R.prepare(self.root, COMMIT, 'v9.0.0', self.root / 'state.json')
        (self.root / 'app/build.gradle').write_text('versionName "1.6.5"\nversionName "2.0.0"\nversionCode 77\n')
        with self.assertRaises(R.ReleaseError): R.version(self.root)

    def test_missing_secrets_report_names_only(self):
        with patch.dict(os.environ, {}, clear=True), self.assertRaises(R.ReleaseError) as error:
            R.check_secrets()
        self.assertIn('MMF_RELEASE_KEYSTORE_BASE64', str(error.exception))
        with patch.dict(os.environ, MMF_RELEASE_KEYSTORE_BASE64='not printed', MMF_RELEASE_STORE_PASSWORD='not printed'):
            R.check_secrets()

    def test_find_release_includes_drafts_and_paginates(self):
        first = [{'id': n, 'tag_name': 'v0.0.0'} for n in range(100)]
        draft = {'id': 123, 'tag_name': 'v1.6.5', 'draft': True}
        with patch.object(R, 'api', side_effect=[first, [draft]]) as api:
            self.assertEqual(R.find_release('v1.6.5'), draft)
        self.assertEqual(api.call_args_list[1].args[0], 'releases?per_page=100&page=2')
        with patch.object(R, 'api', return_value=[]):
            self.assertIsNone(R.find_release('v1.6.5'))

    def test_find_release_rejects_duplicate_drafts_or_public_release(self):
        for public in (False, True):
            duplicates = [{'id': 1, 'tag_name': 'v1.6.5', 'draft': True},
                          {'id': 2, 'tag_name': 'v1.6.5', 'draft': not public}]
            with self.subTest(public=public), patch.object(R, 'api', return_value=duplicates), \
                 self.assertRaises(R.ReleaseError):
                R.find_release('v1.6.5')

    def test_api_preserves_json_boolean_types(self):
        result = type('Result', (), {'returncode': 0, 'stdout': '{"id":123}', 'stderr': ''})()
        with patch.object(R.subprocess, 'run', return_value=result) as native:
            self.assertEqual(R.api('releases', fields={'draft': True}), {'id': 123})
        self.assertEqual(json.loads(native.call_args.kwargs['input']), {'draft': True})
        self.assertIn('--input', native.call_args.args[0])

    def signing_environment(self):
        return {'RUNNER_TEMP': str(self.root), 'MMF_RELEASE_KEYSTORE_BASE64': base64.b64encode(b'fixture key').decode(),
                'MMF_RELEASE_STORE_PASSWORD': 'fixture password'}

    def unsigned(self):
        path = self.root / 'app/build/outputs/apk/release/app-release-unsigned.apk'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'fake APK payload')

    def test_sign_uses_private_temporary_file_and_environment_password_selectors(self):
        self.unsigned()
        observed = []
        def signer(args, **kw):
            observed.append(args)
            key = Path(args[args.index('--ks')+1])
            self.assertEqual(key.read_bytes(), b'fixture key')
            self.assertEqual(key.stat().st_mode & 0o777, 0o600)
            self.assertNotIn('fixture password', [str(a) for a in args])
            self.assertIn('env:MMF_RELEASE_STORE_PASSWORD', args)
            self.assertEqual(args[args.index('--v2-signing-enabled')+1], 'true')
            self.assertNotIn('MMF_RELEASE_KEYSTORE_BASE64', kw['env'])
            self.assertEqual(kw['env']['MMF_RELEASE_KEY_PASSWORD'], 'fixture password')
            Path(args[args.index('--out')+1]).write_bytes(b'fake signed payload')
            return ''
        def verify(*_): self.assertEqual(list(self.root.glob('mmf-sign-*')), [])
        with patch.dict(os.environ, self.signing_environment(), clear=True), \
             patch.object(R, 'sdk_tool', return_value=Path('/fake/apksigner')), \
             patch.object(R, 'verify_package'), patch.object(R, 'verify_apk', side_effect=verify), \
             patch.object(R, 'run', side_effect=signer):
            apk = R.sign(self.state, self.root / 'dist')
        self.assertEqual(len(observed), 1)
        self.assertEqual((apk.parent/'SHA256SUMS.txt').read_text(), f'{R.digest(apk)}  {apk.name}\n')

    def test_sign_cleans_up_on_native_tool_error(self):
        self.unsigned()
        with patch.dict(os.environ, self.signing_environment(), clear=True), \
             patch.object(R, 'sdk_tool', return_value=Path('/fake/apksigner')), \
             patch.object(R, 'verify_package'), patch.object(R, 'run', side_effect=R.ReleaseError('failed')), \
             self.assertRaises(R.ReleaseError):
            R.sign(self.state, self.root/'dist')
        self.assertEqual(list(self.root.glob('mmf-sign-*')), [])
        self.assertFalse((self.root/'dist/SHA256SUMS.txt').exists())

    def test_invalid_base64_never_calls_signer(self):
        self.unsigned(); env = self.signing_environment(); env['MMF_RELEASE_KEYSTORE_BASE64'] = 'not!base64'
        with patch.dict(os.environ, env, clear=True), patch.object(R, 'verify_package'), \
             patch.object(R, 'run') as native, self.assertRaises(R.ReleaseError):
            R.sign(self.state, self.root/'dist')
        native.assert_not_called()

    def test_verify_rejects_wrong_certificate_v1_only_debug_and_wrong_version(self):
        good = "package: name='app.mmfpocket.player' versionCode='77' versionName='1.6.5'"
        signature = f'Signer #1 certificate SHA-256 digest: {R.CERTIFICATE}\nVerified using v2 scheme (APK Signature Scheme v2): true'
        cases = [(good, signature, False), (good, signature.replace(R.CERTIFICATE, '0'*64), True),
                 (good, signature.replace(': true', ': false'), True), (good+'\napplication-debuggable', signature, True),
                 (good.replace("'77'", "'76'"), signature, True),
                 (good.replace("'app.mmfpocket.player'", "'other.app'"), signature, True)]
        for badging, signed, fails in cases:
            with self.subTest(badging=badging, signature=signed), \
                 patch.object(R, 'sdk_tool', side_effect=lambda n: Path('/fake')/n), \
                 patch.object(R, 'run', side_effect=[badging, signed, '']):
                if fails:
                    with self.assertRaises(R.ReleaseError): R.verify_apk(self.root/'file.apk', self.state)
                else: R.verify_apk(self.root/'file.apk', self.state)

    def release_files(self):
        out = self.root/'dist';out.mkdir()
        apk = out/'MMF-Pocket-1.6.5.apk';apk.write_bytes(b'fixture signed APK')
        (out/'SHA256SUMS.txt').write_text(f'{R.digest(apk)}  {apk.name}\n')
        return out

    def test_published_mismatched_or_extra_asset_drafts_are_never_modified(self):
        out = self.release_files()
        for draft in [{'draft': False}, {'draft': True, 'target_commitish': 'b'*40},
                      {'draft': True, 'target_commitish': COMMIT, 'assets': [{'name': 'private.jks'}]}]:
            with self.subTest(draft=draft), patch.object(R, 'verify_apk'), patch.object(R, 'find_release', return_value=draft), \
                 patch.object(R, 'ensure_tag') as tag, patch.object(R, 'run') as commands, self.assertRaises(R.ReleaseError):
                R.publish(self.state, out, True)
            tag.assert_not_called();commands.assert_not_called()

    def test_remote_asset_hashes_are_verified_before_visibility_change(self):
        out = self.release_files()
        for corrupt in [False, True]:
            remote = {}; edited = []; downloaded = set()
            def api(path, **_):
                if path.startswith('actions/'): return self.green
                return {'id': 123, 'draft': not edited, 'target_commitish': COMMIT,
                        'assets': [{'name': n} for n in remote], 'html_url': 'https://example.invalid/release'}
            def command(args, **_):
                if args[:3] == ['gh','release','upload']:
                    p = Path(args[4]);remote[p.name] = p.read_bytes()
                elif args[:3] == ['gh','release','download']:
                    name = args[args.index('--pattern')+1];folder = Path(args[args.index('--dir')+1])
                    (folder/name).write_bytes(b'corrupted' if corrupt else remote[name]);downloaded.add(name)
                elif args[:3] == ['gh','release','edit']:
                    self.assertEqual(downloaded, {'MMF-Pocket-1.6.5.apk','SHA256SUMS.txt'})
                    self.assertIn('--draft=false', args);edited.append(True)
                    self.assertEqual((out/'release-notes.md').read_text(), 'Release notes\n')
                return ''
            with self.subTest(corrupt=corrupt), patch.object(R, 'verify_apk'), patch.object(R, 'ensure_tag'), \
                 patch.object(R, 'find_release', side_effect=lambda _: api('releases/find')), \
                 patch.object(R, 'api', side_effect=api), patch.object(R, 'run', side_effect=command):
                if corrupt:
                    with self.assertRaises(R.ReleaseError): R.publish(self.state, out, True)
                    self.assertFalse(edited)
                else: self.assertEqual(R.publish(self.state, out, True), 'https://example.invalid/release')

    def test_existing_tag_must_resolve_to_exact_commit(self):
        with patch.object(R, 'api', side_effect=[{'object':{}}, {'sha':'b'*40}]), self.assertRaises(R.ReleaseError):
            R.ensure_tag(self.state)

    def test_absent_tag_is_annotated_at_exact_commit(self):
        with patch.object(R, 'api', side_effect=[None, {'sha':'c'*40}, {}, {'sha':COMMIT}]) as api:
            R.ensure_tag(self.state)
        self.assertEqual(api.call_args_list[1].args[1]['object'], COMMIT)
        self.assertEqual(api.call_args_list[1].args[1]['type'], 'commit')
        self.assertEqual(api.call_args_list[2].args[1], {'ref':'refs/tags/v1.6.5','sha':'c'*40})

    def test_new_signed_draft_is_created_without_publishing(self):
        out = self.release_files(); remote = {}; created = []; edits = []
        def api(path, **_):
            if path.startswith('actions/'): return self.green
            if path == 'releases':
                self.assertIs(_['fields']['draft'], True)
                self.assertEqual(_['fields']['target_commitish'], COMMIT)
                created.append(True)
            if not created: return None
            return {'id': 123, 'draft': True, 'target_commitish': COMMIT,
                    'assets':[{'name':n} for n in remote], 'html_url':'https://example.invalid/draft'}
        def command(args, **_):
            if args[:3] == ['gh','release','upload']:
                p=Path(args[4]);remote[p.name]=p.read_bytes()
            elif args[:3] == ['gh','release','download']:
                name=args[args.index('--pattern')+1];directory=Path(args[args.index('--dir')+1])
                (directory/name).write_bytes(remote[name])
            elif args[:3] == ['gh','release','edit']:
                self.assertNotIn('--draft=false', args);self.assertNotIn('--latest', args);edits.append(True)
            return ''
        with patch.object(R,'verify_apk'), patch.object(R,'ensure_tag') as tag, \
             patch.object(R,'find_release',return_value=None) as find, \
             patch.object(R,'api',side_effect=api), patch.object(R,'run',side_effect=command):
            self.assertEqual(R.publish(self.state,out,False),'https://example.invalid/draft')
        tag.assert_called_once_with(self.state);self.assertEqual(len(edits),1)
        find.assert_called_once_with('v1.6.5')

    def test_existing_expected_asset_is_never_replaced_if_different(self):
        out=self.release_files();commands=[]
        draft={'draft':True,'target_commitish':COMMIT,'assets':[{'name':'MMF-Pocket-1.6.5.apk'}]}
        def api(path,**_): return self.green if path.startswith('actions/') else draft
        def command(args,**_):
            commands.append(args)
            if args[:3] == ['gh','release','download']:
                (Path(args[args.index('--dir')+1])/'MMF-Pocket-1.6.5.apk').write_bytes(b'other APK')
            return ''
        with patch.object(R,'verify_apk'), patch.object(R,'ensure_tag'), \
             patch.object(R,'find_release',return_value=draft), \
             patch.object(R,'api',side_effect=api), patch.object(R,'run',side_effect=command), \
             self.assertRaises(R.ReleaseError):
            R.publish(self.state,out,True)
        self.assertTrue(commands)
        self.assertFalse(any(a[:3] in [['gh','release','edit'],['gh','release','upload']] for a in commands))

    def test_failed_commands_do_not_echo_native_output_or_arguments(self):
        class Failed:
            returncode = 1
            stdout = 'fixture secret'
            stderr = 'fixture secret'
        with patch.object(R.subprocess, 'run', return_value=Failed()), self.assertRaises(R.ReleaseError) as error:
            R.run(['/fake/apksigner', 'fixture secret'])
        self.assertNotIn('fixture secret', str(error.exception))


if __name__ == '__main__':
    unittest.main()
