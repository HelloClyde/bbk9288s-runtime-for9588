param(
  [string]$EmulatorRoot = $env:BBK9588_EMULATOR_ROOT,
  [string]$BindAddress = "0.0.0.0",
  [int]$Port = 8013
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($EmulatorRoot)) {
  throw @"
9588 emulator root is required. Pass -EmulatorRoot or set:
  `$env:BBK9588_EMULATOR_ROOT = "C:\path\to\bbk9588-emulator"
"@
}
$EmulatorRoot = [System.IO.Path]::GetFullPath($EmulatorRoot)
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Python = Join-Path $EmulatorRoot "python\python.exe"
$Qemu = Join-Path $EmulatorRoot "bin\bbk9588-qemu-system-mipsel.exe"
$Nand = Join-Path $EmulatorRoot "runtime\bda_test\bbk9588_nand.bin"
$LogDir = Join-Path $ProjectRoot "build\lan"
$StdoutLog = Join-Path $LogDir "scheduled-frontend.stdout.log"
$StderrLog = Join-Path $LogDir "scheduled-frontend.stderr.log"

foreach ($Path in @($Python, $Qemu, $Nand)) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Required 9588 emulator file is missing: $Path"
  }
}

Set-Location -LiteralPath $EmulatorRoot
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Arguments = @(
  "-m", "emu.web.frontend",
  "--boot-mode", "nand",
  "--qemu", $Qemu,
  "--qemu-machine-option", "touch-trace=on",
  "--nand-image", $Nand,
  "--host", $BindAddress,
  "--port", "$Port"
)
$Frontend = Start-Process `
  -FilePath $Python `
  -ArgumentList $Arguments `
  -WorkingDirectory $EmulatorRoot `
  -WindowStyle Hidden `
  -RedirectStandardOutput $StdoutLog `
  -RedirectStandardError $StderrLog `
  -PassThru `
  -Wait
exit $Frontend.ExitCode
