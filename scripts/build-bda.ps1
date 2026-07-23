$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SdkRoot = "E:\eebbk9588"
$Builder = Join-Path $SdkRoot "reverse\bda_compile_c.py"
$Source = Join-Path $ProjectRoot "ports\bbk9588\bda_main.c"
$OutputDir = Join-Path $ProjectRoot "build\bbk9588"
$Output = Join-Path $OutputDir "9288SCompat.bda"

if (-not (Test-Path -LiteralPath $Builder)) {
  throw "9588 BDA SDK not found at $SdkRoot"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

python $Builder `
  $Source `
  --no-template `
  --title "9288SCompat" `
  --category 9 `
  -o $Output
if ($LASTEXITCODE -ne 0) {
  throw "BDA build failed with exit code $LASTEXITCODE"
}

Write-Output "built: $Output"
