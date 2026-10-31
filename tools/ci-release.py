#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""GitHub Actions release gates, isolated signing and guarded publication."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

REPOSITORY = 'Meyonze/mmf-pocket'
PACKAGE = 'app.mmfpocket.player'
CERTIFICATE = '1C79EF3491BD915618F989E053C29648AF1AF0910D9E7150919D0D64D896C59E'


class ReleaseError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise ReleaseError(message)


def run(args, cwd=None, env=None, allowed=(0,)):
    result = subprocess.run([str(a) for a in args], cwd=cwd, env=env,
                            capture_output=True, text=True)
    # Never replay command output/arguments from a signing failure into logs.
    require(result.returncode in allowed, f'{Path(args[0]).name} command failed')
    return result.stdout.strip()


def api(path, fields=None, missing_ok=False):
    args = ['gh', 'api', f'repos/{REPOSITORY}/{path}']
    if fields is not None:
        args += ['--method', 'POST']
        for key, value in fields.items():
            args += ['-f', f'{key}={value}']
    result = subprocess.run(args, capture_output=True, text=True)
    if missing_ok and result.returncode and 'HTTP 404' in result.stderr:
        return None
    require(result.returncode == 0, 'GitHub API request failed')
    return json.loads(result.stdout)


def validate_ref(commit, tag):
    require(re.fullmatch(r'[0-9a-f]{40}', commit) is not None, 'Use a full lowercase commit SHA')
    require(re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+', tag) is not None, 'Use a stable vX.Y.Z tag')


def version(source):
    text = (source / 'app/build.gradle').read_text()
    names = re.findall(r'^\s*versionName\s+"([^"]+)"', text, re.M)
    codes = re.findall(r'^\s*versionCode\s+([0-9]+)\s*$', text, re.M)
    require(len(names) == len(codes) == 1, 'Version metadata is ambiguous')
    require(int(codes[0]) > 0, 'Invalid versionCode')
    return names[0], int(codes[0])


def prepare(source, commit, tag, state_path):
    validate_ref(commit, tag)
    require(os.environ.get('GITHUB_REPOSITORY') == REPOSITORY, 'Unexpected repository')
    source = source.resolve()
    require(run(['git', 'rev-parse', 'HEAD'], cwd=source) == commit, 'Checkout does not match commit')
    run(['git', 'merge-base', '--is-ancestor', commit, 'origin/main'], cwd=source)
    name, code = version(source)
    require(tag == 'v' + name, 'Tag does not match versionName')
    runs = api(f'actions/workflows/android.yml/runs?head_sha={commit}&per_page=10')['workflow_runs']
    matching = [r for r in runs if r['head_sha'] == commit]
    require(matching and matching[0]['status'] == 'completed'
            and matching[0]['conclusion'] == 'success', 'Android CI must succeed for this exact commit')
    notes = source / 'docs' / f'release-notes-{name}.md'
    require(notes.is_file(), 'Release notes are missing for this version')
    state = {'commit': commit, 'tag': tag, 'version': name, 'version_code': code,
             'source': str(source), 'ci_run_id': matching[0]['id']}
    state_path.write_text(json.dumps(state, indent=2) + '\n')
    return state


def check_secrets():
    names = ('MMF_RELEASE_KEYSTORE_BASE64', 'MMF_RELEASE_STORE_PASSWORD')
    missing = [name for name in names if not os.environ.get(name)]
    require(not missing, 'Missing repository secrets: ' + ', '.join(missing))


def load_state(path):
    state = json.loads(path.read_text())
    validate_ref(state['commit'], state['tag'])
    require(state['tag'] == 'v' + state['version'], 'State version mismatch')
    return state


def sdk_tool(name):
    sdk = os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
    require(bool(sdk), 'Android SDK location is not configured')
    tool = Path(sdk) / 'build-tools/35.0.0' / name
    require(tool.is_file(), f'Android build-tools35.0.0 {name} is missing')
    return tool


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_package(apk, state):
    badging = run([sdk_tool('aapt'), 'dump', 'badging', apk])
    require(f"name='{PACKAGE}'" in badging
            and f"versionName='{state['version']}'" in badging
            and f"versionCode='{state['version_code']}'" in badging
            and 'application-debuggable' not in badging, 'Unexpected APK package/version/build type')


def verify_apk(apk, state):
    verify_package(apk, state)
    # APKs with minSdk29 may verify only v3 by default, reporting v2 "false"
    # even when present. Also verify the v2-capable API24 range explicitly.
    # This does not change the APK's manifest/minSdk or supported devices.
    signature = run([sdk_tool('apksigner'), 'verify', '--min-sdk-version', '24',
                     '--verbose', '--print-certs', apk])
    certificates = re.findall(r'Signer #\d+ certificate SHA-256 digest:\s*([0-9a-fA-F]+)', signature)
    require(len(certificates) == 1 and certificates[0].upper() == CERTIFICATE,
            'APK certificate does not match the existing public release')
    require('Verified using v2 scheme (APK Signature Scheme v2): true' in signature,
            'APK v2 signature is required')
    run([sdk_tool('zipalign'), '-c', '-v', '4', apk])


def sign(state, output):
    check_secrets()
    unsigned = Path(state['source']) / 'app/build/outputs/apk/release/app-release-unsigned.apk'
    require(unsigned.is_file(), 'Unsigned release APK is missing')
    verify_package(unsigned, state)
    output.mkdir(parents=True, exist_ok=True)
    apk = output / f"MMF-Pocket-{state['version']}.apk"
    require(not apk.exists(), 'Refusing to overwrite a signed APK')
    runner_temp = os.environ.get('RUNNER_TEMP')
    require(runner_temp and Path(runner_temp).is_dir(), 'Runner temporary directory is required')
    try:
        encoded = ''.join(os.environ['MMF_RELEASE_KEYSTORE_BASE64'].split())
        key_bytes = base64.b64decode(encoded, validate=True)
    except (ValueError, UnicodeError):
        raise ReleaseError('Keystore secret is not valid Base64') from None
    require(0 < len(key_bytes) <= 1024 * 1024, 'Keystore secret has an invalid size')
    # Environment selectors keep passwords out of native process arguments.
    signer_env = dict(os.environ)
    signer_env.pop('MMF_RELEASE_KEYSTORE_BASE64', None)
    signer_env['MMF_RELEASE_KEY_PASSWORD'] = (
        os.environ.get('MMF_RELEASE_KEY_PASSWORD') or os.environ['MMF_RELEASE_STORE_PASSWORD'])
    alias = os.environ.get('MMF_RELEASE_KEY_ALIAS') or 'mmf-pocket'
    with tempfile.TemporaryDirectory(prefix='mmf-sign-', dir=runner_temp) as temporary:
        key = Path(temporary) / 'release.jks'
        key.touch(mode=0o600)
        key.write_bytes(key_bytes)
        del key_bytes
        run([sdk_tool('apksigner'), 'sign', '--ks', key, '--ks-key-alias', alias,
             '--ks-pass', 'env:MMF_RELEASE_STORE_PASSWORD',
             '--key-pass', 'env:MMF_RELEASE_KEY_PASSWORD', '--v2-signing-enabled', 'true',
             '--out', apk, unsigned], env=signer_env)
    # The private key has been removed before verification/publication.
    verify_apk(apk, state)
    (output / 'SHA256SUMS.txt').write_text(f'{digest(apk)}  {apk.name}\n')
    proof = {'commit': state['commit'], 'version': state['version'], 'apk_sha256': digest(apk),
             'certificate_sha256': CERTIFICATE, 'ci_run_id': state['ci_run_id']}
    (output / 'release-proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    return apk


def ensure_tag(state):
    existing = api(f"git/ref/tags/{state['tag']}", missing_ok=True)
    if existing is None:
        tagged = api('git/tags', {'tag': state['tag'], 'message': f"MMF Pocket {state['version']}",
                                  'object': state['commit'], 'type': 'commit'})
        api('git/refs', {'ref': f"refs/tags/{state['tag']}", 'sha': tagged['sha']})
    require(api(f"commits/{state['tag']}")['sha'] == state['commit'], 'Existing tag points elsewhere')


def publish(state, output, make_public):
    apk = output / f"MMF-Pocket-{state['version']}.apk"
    checksums = output / 'SHA256SUMS.txt'
    verify_apk(apk, state)
    require(checksums.read_text() == f'{digest(apk)}  {apk.name}\n', 'Checksum does not match APK')
    release = api(f"releases/tags/{state['tag']}", missing_ok=True)
    if release is not None:
        require(release['draft'], 'Release is already public; refusing modification')
        require(release['target_commitish'] == state['commit'], 'Draft points to another commit')
        require({a['name'] for a in release['assets']} <= {apk.name, checksums.name},
                'Draft contains unexpected assets; refusing to publish them')
    # Check again just before creating a tag or changing a release.
    runs = api(f"actions/workflows/android.yml/runs?head_sha={state['commit']}&per_page=10")['workflow_runs']
    matching = [r for r in runs if r['head_sha'] == state['commit']]
    require(matching and matching[0]['status'] == 'completed' and matching[0]['conclusion'] == 'success',
            'Android CI is not successful for the release commit')
    ensure_tag(state)
    notes = (Path(state['source']) / 'docs' / f"release-notes-{state['version']}.md").read_text()
    notes += f"\nAPK SHA-256: `{digest(apk)}`\n\nSource commit: `{state['commit']}`\n"
    if CERTIFICATE not in notes.upper():
        notes += f'\nSigning certificate SHA-256: `{CERTIFICATE}`\n'
    notes_path = output / 'release-notes.md'
    notes_path.write_text(notes)
    if release is None:
        run(['gh', 'release', 'create', state['tag'], '--repo', REPOSITORY, '--draft',
             '--verify-tag', '--target', state['commit'], '--title', f"MMF Pocket {state['version']}",
             '--notes-file', notes_path])
    existing = {a['name'] for a in release['assets']} if release else set()
    for asset in (apk, checksums):
        if asset.name in existing:
            with tempfile.TemporaryDirectory(prefix='mmf-asset-') as temporary:
                run(['gh', 'release', 'download', state['tag'], '--repo', REPOSITORY,
                     '--pattern', asset.name, '--dir', temporary])
                require(digest(Path(temporary) / asset.name) == digest(asset),
                        'Existing draft asset differs; refusing replacement')
        else:
            run(['gh', 'release', 'upload', state['tag'], asset, '--repo', REPOSITORY])
    # Verify the remote draft payload BEFORE making it public.
    with tempfile.TemporaryDirectory(prefix='mmf-verify-') as temporary:
        for asset in (apk, checksums):
            run(['gh', 'release', 'download', state['tag'], '--repo', REPOSITORY,
                 '--pattern', asset.name, '--dir', temporary])
            require(digest(Path(temporary) / asset.name) == digest(asset), 'Uploaded asset hash mismatch')
    current = api(f"releases/tags/{state['tag']}")
    require(current['draft'] and current['target_commitish'] == state['commit']
            and {a['name'] for a in current['assets']} == {apk.name, checksums.name},
            'Draft changed during verification; refusing to publish')
    args = ['gh', 'release', 'edit', state['tag'], '--repo', REPOSITORY,
            '--verify-tag', '--notes-file', notes_path, '--title', f"MMF Pocket {state['version']}"]
    if make_public:
        args += ['--draft=false', '--prerelease=false', '--latest']
    run(args)
    final = api(f"releases/tags/{state['tag']}")
    require(final['draft'] == (not make_public), 'Unexpected release visibility')
    require({a['name'] for a in final['assets']} == {apk.name, checksums.name}, 'Unexpected release assets')
    with tempfile.TemporaryDirectory(prefix='mmf-verify-') as temporary:
        run(['gh', 'release', 'download', state['tag'], '--repo', REPOSITORY,
             '--pattern', apk.name, '--dir', temporary])
        require(digest(Path(temporary) / apk.name) == digest(apk), 'Uploaded APK hash mismatch')
    return final['html_url']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['validate-ref', 'prepare', 'check-secrets', 'sign', 'publish'])
    parser.add_argument('--source', type=Path)
    parser.add_argument('--commit')
    parser.add_argument('--tag')
    parser.add_argument('--state', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--public', action='store_true')
    args = parser.parse_args()
    if args.command == 'validate-ref':
        validate_ref(args.commit, args.tag)
        print('Release request validated')
    elif args.command == 'prepare':
        prepare(args.source, args.commit, args.tag, args.state)
        print('Source commit, version, main ancestry and exact-commit CI verified')
    elif args.command == 'check-secrets':
        check_secrets()
        print('Required signing secrets are configured')
    elif args.command == 'sign':
        apk = sign(load_state(args.state), args.output)
        print(f'Public signing certificate and APK verified: {apk.name}')
    else:
        print(publish(load_state(args.state), args.output, args.public))


if __name__ == '__main__':
    try:
        main()
    except ReleaseError as error:
        print(f'::error::{error}', file=sys.stderr)
        sys.exit(1)
