param(
    [string]$PropertiesPath = "keystore.properties"
)

$ErrorActionPreference = "Stop"
$properties = Resolve-Path -LiteralPath $PropertiesPath -ErrorAction Stop
$securePassword = Read-Host "Release keystore password" -AsSecureString
$credential = [System.Management.Automation.PSCredential]::new(
    "mmf-pocket", $securePassword)
$plainPassword = $credential.GetNetworkCredential().Password

try {
    $env:MMF_POCKET_KEYSTORE_PROPERTIES = $properties.Path
    $env:MMF_POCKET_STORE_PASSWORD = $plainPassword
    $env:MMF_POCKET_KEY_PASSWORD = $plainPassword
    & .\gradlew.bat clean lint assembleRelease --warning-mode all
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle release build failed with exit code $LASTEXITCODE"
    }
} finally {
    Remove-Item Env:MMF_POCKET_KEYSTORE_PROPERTIES -ErrorAction SilentlyContinue
    Remove-Item Env:MMF_POCKET_STORE_PASSWORD -ErrorAction SilentlyContinue
    Remove-Item Env:MMF_POCKET_KEY_PASSWORD -ErrorAction SilentlyContinue
    $plainPassword = $null
    $credential = $null
    $securePassword = $null
}
