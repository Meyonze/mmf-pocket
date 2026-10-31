param(
    [Parameter(Mandatory = $true)][string]$KeyStorePath,
    [string]$KeyAlias = 'mmf-pocket',
    [string]$ApkSignerPath = '',
    [string]$ManifestPath = (Join-Path $PSScriptRoot 'release-input.json')
)

$ErrorActionPreference = 'Stop'
$manifestFile = (Resolve-Path -LiteralPath $ManifestPath).Path
$directory = Split-Path -Parent $manifestFile
$manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
$inputApk = Join-Path $directory $manifest.unsigned_apk
$outputApk = Join-Path $directory $manifest.signed_apk
$keystore = (Resolve-Path -LiteralPath $KeyStorePath).Path
if ((Get-FileHash -LiteralPath $inputApk -Algorithm SHA256).Hash -ne $manifest.unsigned_sha256) {
    throw 'Input APK checksum does not match the prepared release.'
}
if (Test-Path -LiteralPath $outputApk) {
    throw 'Signed output already exists. Move it aside before running again.'
}
if (-not $ApkSignerPath) {
    $command = Get-Command apksigner.bat -ErrorAction SilentlyContinue
    if ($command) { $ApkSignerPath = $command.Source }
    $roots = @($env:ANDROID_SDK_ROOT, $env:ANDROID_HOME, 'D:\Android\Sdk')
    if ($env:LOCALAPPDATA) { $roots += (Join-Path $env:LOCALAPPDATA 'Android\Sdk') }
    foreach ($sdk in $roots) {
        if ($ApkSignerPath) { break }
        if (-not $sdk) { continue }
        $candidate = Join-Path $sdk 'build-tools\35.0.0\apksigner.bat'
        if (Test-Path -LiteralPath $candidate) { $ApkSignerPath = $candidate }
    }
}
if (-not $ApkSignerPath -or -not (Test-Path -LiteralPath $ApkSignerPath)) {
    throw 'Pass -ApkSignerPath with the path to your Android build-tools apksigner.bat.'
}
# apksigner prompts directly for the existing password; do not put it in arguments.
& $ApkSignerPath sign --ks $keystore --ks-key-alias $KeyAlias --out $outputApk $inputApk
if ($LASTEXITCODE -ne 0) { throw 'APK signing failed.' }
$verification = & $ApkSignerPath verify --verbose --print-certs $outputApk
if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed.' }
$text = $verification | Out-String
$certificate = [regex]::Match($text, 'certificate SHA-256 digest:\s*([0-9a-fA-F]+)')
if (-not $certificate.Success -or $certificate.Groups[1].Value -ne $manifest.certificate_sha256) {
    throw 'Certificate does not match the existing public release. Do not publish this APK.'
}
$hash = (Get-FileHash -LiteralPath $outputApk -Algorithm SHA256).Hash.ToLowerInvariant()
$checksum = Join-Path $directory 'SHA256SUMS.txt'
[System.IO.File]::WriteAllText($checksum, "$hash  $($manifest.signed_apk)`n", [System.Text.UTF8Encoding]::new($false))
Write-Host "Signed APK: $outputApk"
Write-Host "Checksums:  $checksum"
Write-Host 'Return the signed APK to the release operator. Never send the keystore or passwords.'
