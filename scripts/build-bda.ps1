param(
  [string]$SdkRoot = "",
  [string]$ToolchainPrefix = $env:BDA_TOOLCHAIN_PREFIX,
  [string]$Python = "python",
  [switch]$SkipToolchainSetup
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
  $SdkRoot = Join-Path $ProjectRoot "sdk"
}
$SdkRoot = [System.IO.Path]::GetFullPath($SdkRoot)
$SdkHeader = Join-Path $SdkRoot "sdk\include\bda_dialogs.h"
$ToolchainSetup = Join-Path $SdkRoot "scripts\setup_toolchain.ps1"
$Source = Join-Path $ProjectRoot "ports\bbk9588\bda_main.c"
$OutputDir = Join-Path $ProjectRoot "build\bbk9588"
$Output = Join-Path $OutputDir "9288SCompat.bda"

if (-not (Test-Path -LiteralPath $SdkHeader)) {
  throw @"
9588 BDA SDK not found at $SdkRoot.
Initialize the sdk submodule with:
  git submodule update --init sdk
"@
}

function Find-ToolchainPrefix {
  $Candidates = @(
    (Join-Path $SdkRoot ".toolchain\bin\mipsel-none-elf-"),
    (Join-Path $SdkRoot ".toolchain\g++-mipsel-none-elf-15.2.0\bin\mipsel-none-elf-"),
    (Join-Path $SdkRoot "tools\bin\mipsel-none-elf-"),
    (Join-Path $SdkRoot "tools\g++-mipsel-none-elf-15.2.0\bin\mipsel-none-elf-")
  )
  foreach ($Candidate in $Candidates) {
    if (Test-Path -LiteralPath "${Candidate}gcc.exe" -PathType Leaf) {
      return $Candidate
    }
  }
  return $null
}

if ([string]::IsNullOrWhiteSpace($ToolchainPrefix)) {
  $ToolchainPrefix = Find-ToolchainPrefix
}
if (
  [string]::IsNullOrWhiteSpace($ToolchainPrefix) -and
  -not $SkipToolchainSetup
) {
  if (-not (Test-Path -LiteralPath $ToolchainSetup -PathType Leaf)) {
    throw "SDK toolchain setup script not found: $ToolchainSetup"
  }
  & $ToolchainSetup
  if ($LASTEXITCODE -ne 0) {
    throw "9588 SDK toolchain setup failed with exit code $LASTEXITCODE"
  }
  $ToolchainPrefix = Find-ToolchainPrefix
}
if ([string]::IsNullOrWhiteSpace($ToolchainPrefix)) {
  throw @"
MIPS toolchain not found. Run:
  .\sdk\scripts\setup_toolchain.ps1
or pass -ToolchainPrefix / set BDA_TOOLCHAIN_PREFIX.
"@
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$PreviousPythonPath = $env:PYTHONPATH
try {
  if ([string]::IsNullOrWhiteSpace($PreviousPythonPath)) {
    $env:PYTHONPATH = $SdkRoot
  } else {
    $env:PYTHONPATH = $SdkRoot +
      [System.IO.Path]::PathSeparator +
      $PreviousPythonPath
  }

  & $Python `
    -m bda_packer `
    $Source `
    --title "9288SCompat" `
    --category 4 `
    --prefix $ToolchainPrefix `
    -o $Output
  if ($LASTEXITCODE -ne 0) {
    throw "BDA build failed with exit code $LASTEXITCODE"
  }
} finally {
  if ($null -eq $PreviousPythonPath) {
    Remove-Item Env:\PYTHONPATH -ErrorAction SilentlyContinue
  } else {
    $env:PYTHONPATH = $PreviousPythonPath
  }
}

Write-Output "runtime: selectable 9288S D300 EXE"
Write-Output "sdk: $SdkRoot"
Write-Output "built: $Output"
