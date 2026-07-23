param(
  [string]$GamePath = "",
  [string]$EmulatorUrl = "http://127.0.0.1:8013",
  [switch]$NoBrowser
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $PSScriptRoot "build-bda.ps1"
$OutputDir = Join-Path $ProjectRoot "build\bbk9588"
$BdaPath = Join-Path $OutputDir "9288SCompat.bda"
$BackupDir = Join-Path $OutputDir "backup"
$BackupPath = Join-Path $BackupDir "宠物单词.bda"
$ScreenshotPath = Join-Path $OutputDir "pirate-running.png"
$TargetDirectory = "/应用/程序"
$TargetName = "宠物单词.bda"
$TargetPath = "$TargetDirectory/$TargetName"

function Invoke-EmulatorCommand {
  param([hashtable]$Command)
  $Body = $Command | ConvertTo-Json -Compress
  Invoke-RestMethod `
    -Method Post `
    -Uri "$EmulatorUrl/api/command" `
    -ContentType "application/json" `
    -Body $Body `
    -TimeoutSec 30
}

function Send-EmulatorKey {
  param(
    [int]$Code,
    [int]$HoldMilliseconds = 300
  )
  Invoke-EmulatorCommand @{
    op = "key"
    code = $Code
    down = $true
    reply = $false
  } | Out-Null
  Start-Sleep -Milliseconds $HoldMilliseconds
  Invoke-EmulatorCommand @{
    op = "key"
    code = $Code
    down = $false
    reply = $false
  } | Out-Null
}

try {
  $Status = Invoke-RestMethod `
    -Uri "$EmulatorUrl/api/status" `
    -TimeoutSec 10
} catch {
  throw "9588 emulator frontend is not reachable at $EmulatorUrl"
}

if ([string]::IsNullOrWhiteSpace($GamePath)) {
  & $BuildScript
} else {
  & $BuildScript -GamePath $GamePath
}
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $BdaPath)) {
  throw "Compatibility BDA build failed"
}

New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
if (-not (Test-Path -LiteralPath $BackupPath)) {
  $EncodedTarget = [uri]::EscapeDataString($TargetPath)
  try {
    Invoke-WebRequest `
      -Uri "$EmulatorUrl/api/files/export?path=$EncodedTarget" `
      -OutFile $BackupPath `
      -TimeoutSec 60
    Write-Output "backup: $BackupPath"
  } catch {
    Remove-Item -LiteralPath $BackupPath -Force -ErrorAction SilentlyContinue
    Write-Warning "The original $TargetName could not be backed up: $($_.Exception.Message)"
  }
}

if ($Status.running) {
  Invoke-EmulatorCommand @{ op = "force-stop" } | Out-Null
}

$EncodedDirectory = [uri]::EscapeDataString($TargetDirectory)
$EncodedName = [uri]::EscapeDataString($TargetName)
Invoke-WebRequest `
  -Method Post `
  -Uri "$EmulatorUrl/api/files/import?path=$EncodedDirectory&name=$EncodedName" `
  -ContentType "application/octet-stream" `
  -InFile $BdaPath `
  -TimeoutSec 90 | Out-Null

Write-Output "installed: $TargetPath"
Write-Output "waiting for the 9588 desktop..."
Start-Sleep -Seconds 12

# The reset desktop selects 查询典. One Right selects 背单词/E-pets, whose
# fixed launcher filename is replaced by this compatibility BDA.
Send-EmulatorKey -Code 7
Start-Sleep -Seconds 1
Send-EmulatorKey -Code 10
Start-Sleep -Seconds 5

Invoke-WebRequest `
  -Uri "$EmulatorUrl/screen.png" `
  -OutFile $ScreenshotPath `
  -TimeoutSec 30

if (-not $NoBrowser) {
  Start-Process "$EmulatorUrl/"
}

Write-Output "running: original 9288S 海盗船 title screen"
Write-Output "screen: $ScreenshotPath"
Write-Output "controls: W/A/S/D or arrow buttons; J/确定; K/退出"
