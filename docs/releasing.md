# Release procedure

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

## Build and verify

```powershell
$env:JAVA_HOME = 'D:\Android\Jdk17'
.\gradlew.bat clean lint assembleRelease
```

Before publishing:

1. Confirm the APK is signed with the release certificate, not the Android debug certificate.
2. Install it on at least one physical Android 10+ device.
3. Confirm folder selection, conversion, cache reuse, player controls and the 携帯スピーカー風 mode.
4. Record the APK SHA-256 and certificate SHA-256 fingerprint.
5. Tag the exact commit and create a prerelease on GitHub.

Do not upload `keystore.properties`, a keystore, sample MMFs or generated WAV files.

## GitHub release notes

Mark beta builds as a prerelease. Attach only the signed universal APK and a
SHA-256 checksum file. Include the signing certificate SHA-256 fingerprint in
the release text. Do not distribute the debug APK produced by CI.
