param(
  [string]$ReleaseRoot = "E:\bbk9588-emulator-v0.1.5",
  [int]$Port = 8013
)

$ErrorActionPreference = "Stop"

$Principal = [Security.Principal.WindowsPrincipal](
  [Security.Principal.WindowsIdentity]::GetCurrent()
)
if (-not $Principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
  )) {
  $ElevatedArguments = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", "`"$PSCommandPath`"",
    "-ReleaseRoot", "`"$ReleaseRoot`"",
    "-Port", "$Port"
  )
  $Elevated = Start-Process `
    -FilePath "powershell.exe" `
    -Verb RunAs `
    -ArgumentList $ElevatedArguments `
    -Wait `
    -PassThru
  exit $Elevated.ExitCode
}

$RuleName = "BBK9588 Emulator LAN $Port"
$Python = Join-Path $ReleaseRoot "python\python.exe"
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
  throw "Bundled Python was not found at $Python"
}

$Existing = Get-NetFirewallRule `
  -DisplayName $RuleName `
  -ErrorAction SilentlyContinue
if ($Existing) {
  Set-NetFirewallRule `
    -DisplayName $RuleName `
    -Enabled True `
    -Direction Inbound `
    -Action Allow `
    -Profile Private `
    -RemoteAddress LocalSubnet | Out-Null
} else {
  New-NetFirewallRule `
    -DisplayName $RuleName `
    -Description "Allow the BBK 9588 emulator Web frontend from the private LAN only." `
    -Enabled True `
    -Direction Inbound `
    -Action Allow `
    -Profile Private `
    -Program $Python `
    -Protocol TCP `
    -LocalPort $Port `
    -RemoteAddress LocalSubnet | Out-Null
}

Write-Output "firewall: $RuleName"
