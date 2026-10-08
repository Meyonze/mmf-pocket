param(
    [Parameter(Mandatory=$true)][string]$BaselineReport,
    [Parameter(Mandatory=$true)][string]$CandidateReport,
    [switch]$RequireUnchangedHandyPhone,
    [switch]$RequireUnchangedMobile,
    [switch]$ListChanges
)
$ErrorActionPreference = 'Stop'

# Read-only comparison of corpus-test memory-render reports. No audio or MMF
# data is exported. Use identical inputs, sample rate, probe duration/compiler.
function Read-AudioReport([string]$Path) {
    $taskRows = @(Import-Csv -LiteralPath $Path -Delimiter "`t")
    if ($taskRows.Count -eq 0) { throw "Empty audio report: $Path" }
    $taskIndex = @{}
    foreach ($taskRow in $taskRows) {
        foreach ($taskField in @('status','file','tracks','probed_seconds','float_hash')) {
            if (!($taskRow.PSObject.Properties.Name -contains $taskField)) {
                throw "Missing $taskField in $Path. Rebuild corpus-test with fingerprint support."
            }
        }
        if ([string]::IsNullOrWhiteSpace($taskRow.file) -or $taskIndex.ContainsKey($taskRow.file)) {
            throw "Missing or duplicate file key in $Path"
        }
        if ($taskRow.status -notin @('ok','ok_probe_silent') -or $taskRow.float_hash -notmatch '^[0-9a-fA-F]{1,16}$') {
            throw "Invalid render/fingerprint for $($taskRow.file) in $Path"
        }
        $taskSeconds = 0.0
        if (![double]::TryParse($taskRow.probed_seconds,
            [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture,[ref]$taskSeconds) -or
            [double]::IsNaN($taskSeconds) -or [double]::IsInfinity($taskSeconds) -or $taskSeconds -lt 0) {
            throw "Invalid probe duration in $Path"
        }
        $taskIndex[$taskRow.file] = $taskRow
    }
    return ,$taskIndex
}
function Get-AudioProfile([string]$Tracks) {
    # Match the format field, NOT the number3 elsewhere in a track description.
    if ($Tracks -match '(^|,)M\d+:3:') { return 'MA7' }
    if ($Tracks -match '(^|,)M\d+:[12]:') { return 'Mobile' }
    if ($Tracks -match '(^|,)M\d+:0:') { return 'HandyPhone' }
    return 'Other'
}

$taskBefore = Read-AudioReport $BaselineReport
$taskAfter = Read-AudioReport $CandidateReport
if ($taskBefore.Count -ne $taskAfter.Count) { throw 'Report file sets differ.' }
$taskCounts = @{HandyPhone=0;Mobile=0;MA7=0;Other=0}
$taskChanged = @{HandyPhone=0;Mobile=0;MA7=0;Other=0}
$taskChanges = @()
foreach ($taskKey in $taskBefore.Keys) {
    if (!$taskAfter.ContainsKey($taskKey)) { throw "Candidate is missing $taskKey" }
    $taskOld = $taskBefore[$taskKey]; $taskNew = $taskAfter[$taskKey]
    if ($taskOld.tracks -ne $taskNew.tracks) { throw "Input track description changed: $taskKey" }
    $taskProfile = Get-AudioProfile $taskNew.tracks
    ++$taskCounts[$taskProfile]
    # FNV hashes include every returned stereo float in order. This is a change
    # detector, not a perceptual metric or a cryptographic identity check.
    if ($taskOld.float_hash -ne $taskNew.float_hash -or
        $taskOld.probed_seconds -ne $taskNew.probed_seconds) {
        ++$taskChanged[$taskProfile]
        $taskChanges += [pscustomobject]@{File=$taskKey;Profile=$taskProfile;
            Before=$taskOld.float_hash;After=$taskNew.float_hash}
    }
}
foreach ($taskProfile in @('HandyPhone','Mobile','MA7','Other')) {
    [pscustomobject]@{Profile=$taskProfile;Files=$taskCounts[$taskProfile];
        Changed=$taskChanged[$taskProfile];Unchanged=$taskCounts[$taskProfile]-$taskChanged[$taskProfile]}
}
if ($ListChanges) { $taskChanges | Sort-Object File }
if ($RequireUnchangedHandyPhone -and $taskChanged.HandyPhone -ne 0) {
    throw 'HandyPhone output changed unexpectedly.'
}
if ($RequireUnchangedMobile -and $taskChanged.Mobile -ne 0) {
    throw 'Mobile MA3/5 output changed unexpectedly.'
}
