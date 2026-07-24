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
$ScreenshotPath = Join-Path $OutputDir "file-selector.png"
$ProjectGame = Join-Path $ProjectRoot "game\pirate.exe"
$AppsName = -join @([char]0x5e94, [char]0x7528)
$ProgramsName = -join @([char]0x7a0b, [char]0x5e8f)
$LauncherName = -join @(
  [char]0x5ba0, [char]0x7269, [char]0x5355, [char]0x8bcd
)
$SystemName = -join @([char]0x7cfb, [char]0x7edf)
$FilesName = -join @([char]0x6587, [char]0x4ef6)
$PirateName = -join @([char]0x6d77, [char]0x76d7, [char]0x8239)
$BbkFolder = (-join @(
  [char]0x6b65, [char]0x6b65, [char]0x9ad8
)) + "9288s$SystemName$FilesName"
$OriginalGame = "D:\Downloads\$BbkFolder\$SystemName\$ProgramsName\$PirateName.exe"
$TargetDirectory = "/$AppsName/$ProgramsName"
$TargetName = "$LauncherName.bda"
$TargetPath = "$TargetDirectory/$TargetName"
$BackupPath = Join-Path $BackupDir $TargetName

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

& $BuildScript
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $BdaPath)) {
  throw "Compatibility BDA build failed"
}

if ([string]::IsNullOrWhiteSpace($GamePath)) {
  if (Test-Path -LiteralPath $ProjectGame) {
    $GamePath = $ProjectGame
  } elseif (Test-Path -LiteralPath $OriginalGame) {
    $GamePath = $OriginalGame
  }
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

if (-not [string]::IsNullOrWhiteSpace($GamePath)) {
  if (-not (Test-Path -LiteralPath $GamePath -PathType Leaf)) {
    throw "9288S EXE not found: $GamePath"
  }
  $GameName = [System.IO.Path]::GetFileName($GamePath)
  $EncodedGameDirectory = [uri]::EscapeDataString("/")
  $EncodedGameName = [uri]::EscapeDataString($GameName)
  Invoke-WebRequest `
    -Method Post `
    -Uri "$EmulatorUrl/api/files/import?path=$EncodedGameDirectory&name=$EncodedGameName" `
    -ContentType "application/octet-stream" `
    -InFile $GamePath `
    -TimeoutSec 90 | Out-Null
  Write-Output "available in selector: A:\$GameName"
  Invoke-EmulatorCommand @{ op = "force-stop" } | Out-Null
  Start-Sleep -Seconds 2
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

Write-Output "running: 9288S EXE file selector"
Write-Output "screen: $ScreenshotPath"
Write-Output "select an EXE, then use the on-screen D-pad/A/B controls"
