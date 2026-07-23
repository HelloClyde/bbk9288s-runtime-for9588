param(
  [string]$ReleaseRoot = "E:\bbk9588-emulator-v0.1.5",
  [string]$BindAddress = "0.0.0.0",
  [int]$Port = 8013
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Python = Join-Path $ReleaseRoot "python\python.exe"
$Qemu = Join-Path $ReleaseRoot "bin\bbk9588-qemu-system-mipsel.exe"
$Nand = Join-Path $ReleaseRoot "runtime\bda_test\bbk9588_nand.bin"
$LogDir = Join-Path $ProjectRoot "build\lan"
$StdoutLog = Join-Path $LogDir "frontend.stdout.log"
$StderrLog = Join-Path $LogDir "frontend.stderr.log"
$PidPath = Join-Path $LogDir "frontend.pid"

foreach ($Path in @($Python, $Qemu, $Nand)) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Required 9588 emulator file is missing: $Path"
  }
}

$Existing = Get-NetTCPConnection `
  -State Listen `
  -LocalPort $Port `
  -ErrorAction SilentlyContinue |
  Select-Object -First 1
if ($Existing) {
  $ProcessInfo = Get-CimInstance `
    Win32_Process `
    -Filter "ProcessId=$($Existing.OwningProcess)"
  if (
    -not $ProcessInfo -or
    $ProcessInfo.CommandLine -notlike "*emu.web.frontend*" -or
    $ProcessInfo.ExecutablePath -ine $Python
  ) {
    throw "Port $Port is occupied by an unrelated process"
  }
  try {
    Invoke-RestMethod `
      -Method Post `
      -Uri "http://127.0.0.1:$Port/api/command" `
      -ContentType "application/json" `
      -Body '{"op":"force-stop"}' `
      -TimeoutSec 30 | Out-Null
  } catch {
    Write-Warning "The old frontend did not stop QEMU cleanly: $($_.Exception.Message)"
  }
  Stop-Process -Id $Existing.OwningProcess -Force
  $Deadline = [DateTime]::UtcNow.AddSeconds(10)
  while (
    (Get-NetTCPConnection `
      -State Listen `
      -LocalPort $Port `
      -ErrorAction SilentlyContinue) -and
    [DateTime]::UtcNow -lt $Deadline
  ) {
    Start-Sleep -Milliseconds 200
  }
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$env:PYTHONNOUSERSITE = "1"
$Arguments = @(
  "-m", "emu.web.frontend",
  "--boot-mode", "nand",
  "--qemu", $Qemu,
  "--nand-image", $Nand,
  "--host", $BindAddress,
  "--port", "$Port"
)
$Frontend = Start-Process `
  -FilePath $Python `
  -ArgumentList $Arguments `
  -WorkingDirectory $ReleaseRoot `
  -WindowStyle Hidden `
  -RedirectStandardOutput $StdoutLog `
  -RedirectStandardError $StderrLog `
  -PassThru
Set-Content -LiteralPath $PidPath -Value $Frontend.Id -Encoding ASCII

$Ready = $false
$Deadline = [DateTime]::UtcNow.AddSeconds(30)
while (-not $Ready -and [DateTime]::UtcNow -lt $Deadline) {
  Start-Sleep -Milliseconds 500
  try {
    $Status = Invoke-RestMethod `
      -Uri "http://127.0.0.1:$Port/api/status" `
      -TimeoutSec 2
    $Ready = $null -ne $Status
  } catch {
    $Ready = $false
  }
}
if (-not $Ready) {
  throw "9588 LAN frontend did not become ready; inspect $StderrLog"
}

$LanAddresses = Get-NetIPAddress -AddressFamily IPv4 |
  Where-Object {
    $_.AddressState -eq "Preferred" -and
    $_.IPAddress -notlike "127.*" -and
    $_.IPAddress -notlike "169.254.*" -and
    $_.InterfaceAlias -notlike "vEthernet*" -and
    $_.InterfaceAlias -ne "Meta"
  } |
  Select-Object -ExpandProperty IPAddress -Unique

Write-Output "listening: ${BindAddress}:$Port"
Write-Output "local: http://127.0.0.1:$Port/"
foreach ($Address in $LanAddresses) {
  Write-Output "lan: http://${Address}:$Port/"
}
