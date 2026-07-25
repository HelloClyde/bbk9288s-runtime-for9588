param(
  [Parameter(Mandatory = $true)]
  [string]$Image
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build\host-probe"
$ProbeExe = Join-Path $BuildDir "d300-core-probe.exe"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

gcc `
  -std=c11 `
  -Wall `
  -Wextra `
  -Werror `
  -I (Join-Path $ProjectRoot "runtime\include") `
  (Join-Path $ProjectRoot "runtime\src\d300.c") `
  (Join-Path $ProjectRoot "runtime\src\c33vm.c") `
  (Join-Path $ProjectRoot "runtime\src\c33jit.c") `
  (Join-Path $ProjectRoot "runtime\src\compat_api.c") `
  (Join-Path $ProjectRoot "tools\d300_core_probe.c") `
  -o $ProbeExe
if ($LASTEXITCODE -ne 0) {
  throw "probe build failed with exit code $LASTEXITCODE"
}

& $ProbeExe $Image
exit $LASTEXITCODE
