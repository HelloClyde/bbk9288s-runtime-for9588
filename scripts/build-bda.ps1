$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SdkRoot = "C:\Users\ASUS\Documents\eebbk9588_native_sdk"
$SdkHeader = Join-Path $SdkRoot "sdk\include\bda_dialogs.h"
$Source = Join-Path $ProjectRoot "ports\bbk9588\bda_main.c"
$OutputDir = Join-Path $ProjectRoot "build\bbk9588"
$Output = Join-Path $OutputDir "9288SCompat.bda"

if (-not (Test-Path -LiteralPath $SdkHeader)) {
  throw "9588 BDA SDK not found at $SdkRoot"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

python -m bda_packer `
  $Source `
  --title "9288SCompat" `
  --category 9 `
  -o $Output
if ($LASTEXITCODE -ne 0) {
  throw "BDA build failed with exit code $LASTEXITCODE"
}

Write-Output "runtime: selectable 9288S D300 EXE"
Write-Output "built: $Output"
