# Release procedure

## GitHub Actions signing and publication

Use [Signed release](../.github/workflows/release.yml) to build, sign and publish
without transferring the private key to this chat or signing every release on
Windows. Register the EXISTING public release key, not a new/debug key.

In [repository Actions secrets](https://github.com/Meyonze/mmf-pocket/settings/secrets/actions),
add these two repository secrets once:

| Name | Value |
| --- | --- |
| `MMF_RELEASE_KEYSTORE_BASE64` | Base64 of the existing release `.jks` file |
| `MMF_RELEASE_STORE_PASSWORD` | Existing keystore password |

On Windows, copy the key's Base64 directly to the clipboard without printing it:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes('D:\path\to\existing-release.jks')) | Set-Clipboard
```

Paste it only into the GitHub secret field. Enter the password directly in its
secret field. Do not put either value in chat, a commit, release notes or logs.
If the key entry password differs, add `MMF_RELEASE_KEY_PASSWORD`; otherwise the
workflow reuses the store password. The alias defaults to `mmf-pocket`; if yours
differs, set the repository Actions variable `MMF_RELEASE_KEY_ALIAS`.

Run Signed release from `main` with the intended source commit and version:

- commit: the full commit SHA with successful Android CI.
- tag: `v1.6.6` (must match that commit's versionName).
- publish: enabled (disable to keep the signed release as a draft).

The workflow checks main ancestry, version/tag agreement and successful Android
CI for that exact SHA. It builds without signing credentials in Gradle's
environment, then restores the key only in a private runner temporary directory
for signing and removes it afterward. It verifies package/version, non-debug
build type, alignment, v2 signing and the existing public certificate fingerprint
`1C79EF3491BD915618F989E053C29648AF1AF0910D9E7150919D0D64D896C59E`.
Only the signed universal APK and SHA256SUMS.txt are attached. Both uploaded draft
assets are downloaded and checked before publication. Existing public releases,
different tag targets and conflicting draft assets are not replaced.
Drafts are found through the release list and checked by ID; duplicate releases
using the same tag stop publication. Release descriptions use the concise version
notes; checksums remain in the attached SHA256SUMS.txt.

After the one-time setup, the release can be dispatched from GitHub or by the
assistant. No Windows signing or key transfer is needed for subsequent releases.
An unset/malformed key or different certificate stops the workflow; it does not
fall back to debug signing. The fast Release automation checks workflow uses
fake native tools/APIs and no signing secrets; it does not claim real signing.

## One-time signing setup

Create a dedicated keystore outside the repository. Never commit the keystore or its passwords.

Create `keystore.properties` in the repository root or point the `MMF_POCKET_KEYSTORE_PROPERTIES` environment variable to a properties file outside the repository:

```properties
storeFile=D:/path/outside/repository/mmf-pocket-release.jks
storePassword=...
keyAlias=mmf-pocket
keyPassword=...
```

Back up the keystore and password separately. Losing the signing key prevents normal updates to existing installations.

The passwords may instead be supplied only for the current process through
`MMF_POCKET_STORE_PASSWORD` and `MMF_POCKET_KEY_PASSWORD`. On Windows,
`tools/build-signed-release.ps1` prompts without echoing the password and sets
those variables only while Gradle is running. The current release key uses the
same password for the keystore and key entry.

### Sign a cloud-prepared APK on Windows

A prepared signing bundle contains the unsigned release APK, `release-input.json`
and `sign-prebuilt-release.ps1`. Extract them into one folder and run:

```powershell
.\sign-prebuilt-release.ps1 -KeyStorePath 'D:\path\to\existing-release.jks'
```

If needed, specify `-ApkSignerPath` pointing to Android build-tools35.0.0's
`apksigner.bat`, or `-KeyAlias` for the existing alias. The tool prompts for the
password locally; do not put it in chat or command arguments. It checks the
input hash and verifies the output certificate against the existing public
release before creating `SHA256SUMS.txt`. It does not publish anything.
Return only the signed APK to the release operator; never transfer the signing
key or passwords. This Windows wrapper is not covered by Linux host execution.

## Build and verify

```powershell
$env:JAVA_HOME = 'D:\Android\Jdk17'
.\tools\build-signed-release.ps1
```

Before publishing:

1. Confirm the APK is signed with the release certificate, not the Android debug certificate.
2. Install it on at least one physical Android 10+ device.
3. Confirm folder selection, conversion, cache reuse, player controls and the 携帯スピーカー風 mode.
4. Record the APK SHA-256 and certificate SHA-256 fingerprint.
5. Tag the exact commit and create a release on GitHub. Mark beta builds as a
   prerelease; stable builds use a normal release.

Do not upload `keystore.properties`, a keystore, sample MMFs or generated WAV files.

## GitHub release notes

Attach only the signed universal APK and a SHA-256 checksum file. Include the
signing certificate SHA-256 fingerprint in the release text. Mark beta builds
as a prerelease. Do not distribute the debug APK produced by CI.
