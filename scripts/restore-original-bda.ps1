param(
  [string]$EmulatorUrl = "http://127.0.0.1:8013"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$AppsName = -join @([char]0x5e94, [char]0x7528)
$ProgramsName = -join @([char]0x7a0b, [char]0x5e8f)
$LauncherName = -join @(
  [char]0x5ba0, [char]0x7269, [char]0x5355, [char]0x8bcd
)
$TargetDirectory = "/$AppsName/$ProgramsName"
$TargetName = "$LauncherName.bda"
$BackupPath = Join-Path $ProjectRoot "build\bbk9588\backup\$TargetName"

if (-not (Test-Path -LiteralPath $BackupPath)) {
  throw "Original BDA backup not found at $BackupPath"
}

try {
  $Status = Invoke-RestMethod `
    -Uri "$EmulatorUrl/api/status" `
    -TimeoutSec 10
} catch {
  throw "9588 emulator frontend is not reachable at $EmulatorUrl"
}

if ($Status.running) {
  Invoke-RestMethod `
    -Method Post `
    -Uri "$EmulatorUrl/api/command" `
    -ContentType "application/json" `
    -Body '{"op":"force-stop"}' `
    -TimeoutSec 30 | Out-Null
}

$EncodedDirectory = [uri]::EscapeDataString($TargetDirectory)
$EncodedName = [uri]::EscapeDataString($TargetName)
Invoke-WebRequest `
  -Method Post `
  -Uri "$EmulatorUrl/api/files/import?path=$EncodedDirectory&name=$EncodedName" `
  -ContentType "application/octet-stream" `
  -InFile $BackupPath `
  -TimeoutSec 90 | Out-Null

Write-Output "restored: $TargetDirectory/$TargetName"
