#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build a separately installed test APK without editing production sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def run(source, output, offline=False):
    source = source.resolve(strict=True)
    output = output.resolve()
    if output.exists():
        raise ValueError('use a new output directory')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
    java = (source / 'app/src/main/java/app/mmfpocket/player/MmfAudioRepository.java').read_text()
    cache = re.search(r'RENDER_CACHE_VERSION = "(r[0-9]+)"', java)[1]
    version = re.search(r'versionName "([0-9.]+)"', (source / 'app/build.gradle').read_text())[1]
    suffix = f'-{cache}-test'
    with tempfile.TemporaryDirectory(prefix='mmf-test-build-') as temporary:
        temp = Path(temporary)
        resources = temp / 'res'
        shutil.copytree(source / 'app/src/debug/res', resources)
        identity = resources / 'values/debug_identity.xml'
        identity.write_text(identity.read_text().replace('MMF Pocket DEV', 'MMF Pocket TEST'))
        init = temp / 'test.init.gradle'
        init.write_text('''gradle.beforeProject { p ->
    p.plugins.withId('com.android.application') {
        p.android.buildTypes.named('debug') {
            applicationIdSuffix = '.test'
            versionNameSuffix = ''' + json.dumps(suffix) + '''
        }
        p.android.sourceSets.named('debug') {
            res.setSrcDirs([''' + json.dumps(str(resources)) + '''])
        }
    }
}
''')
        command = ['bash', 'gradlew', '--no-daemon', '--max-workers=2', '--init-script', str(init)]
        if offline:
            command.append('--offline')
        subprocess.run(command + ['lintDebug', 'assembleDebug'], cwd=source, check=True)
    sdk = Path(os.environ.get('ANDROID_HOME') or os.environ['ANDROID_SDK_ROOT'])
    tools = sdk / 'build-tools/35.0.0'
    apk = source / 'app/build/outputs/apk/debug/app-debug.apk'
    subprocess.run([str(tools / 'apksigner'), 'verify', '--min-sdk-version', '29', str(apk)], check=True)
    badging = subprocess.check_output([str(tools / 'aapt'), 'dump', 'badging', str(apk)], text=True)
    if ("name='app.mmfpocket.player.test'" not in badging
            or f"versionName='{version}{suffix}'" not in badging
            or "application-label:'MMF Pocket TEST'" not in badging):
        raise ValueError('test APK identity verification failed')
    output.mkdir(parents=True)
    target = output / f'MMF-Pocket-{version}{suffix}.apk'
    shutil.copy2(apk, target)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    (output / 'SHA256SUMS.txt').write_text(f'{digest}  {target.name}\n')
    (output / 'build-info.json').write_text(json.dumps(dict(source_commit=commit, cache=cache,
        version=version + suffix, package='app.mmfpocket.player.test', sha256=digest,
        signing='Android debug key; separate package from official release'), indent=2) + '\n')
    print(f'Test APK verified: {target}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path('.'))
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    run(args.source, args.output, args.offline)
