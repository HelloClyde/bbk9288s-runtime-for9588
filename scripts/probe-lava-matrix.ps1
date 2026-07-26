param(
  [Parameter(Mandatory = $true)]
  [string]$Image,
  [ValidateRange(1, 1000000)]
  [int]$Messages = 32768
)

$ErrorActionPreference = "Stop"
$ProbeScript = Join-Path $PSScriptRoot "probe-d300.ps1"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProbeExe = Join-Path $ProjectRoot "build\host-probe\d300-core-probe.exe"

# Build once through the regular probe entry point.
& $ProbeScript `
  -Image $Image `
  -Messages $Messages `
  -LavIndex 0 `
  -Gameplay `
  -Quiet
if ($LASTEXITCODE -ne 0) {
  throw "Lava probe index 0 failed with exit code $LASTEXITCODE"
}

foreach ($LavIndex in 1..4) {
  & $ProbeExe `
    --quiet `
    --gameplay `
    --messages $Messages `
    --lav-index $LavIndex `
    $Image
  if ($LASTEXITCODE -ne 0) {
    throw "Lava probe index $LavIndex failed with exit code $LASTEXITCODE"
  }
}
