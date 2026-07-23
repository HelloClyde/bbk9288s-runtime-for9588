param(
  [string]$GamePath = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SdkRoot = "E:\eebbk9588"
$Builder = Join-Path $SdkRoot "reverse\bda_compile_c.py"
$Source = Join-Path $ProjectRoot "ports\bbk9588\bda_main.c"
$OutputDir = Join-Path $ProjectRoot "build\bbk9588"
$Output = Join-Path $OutputDir "9288SCompat.bda"
$GeneratedGame = Join-Path $OutputDir "generated_game.inc"
$ProjectGame = Join-Path $ProjectRoot "game\pirate.exe"
$OriginalGame = "D:\Downloads\步步高9288s系统文件\系统\程序\海盗船.exe"

if (-not (Test-Path -LiteralPath $Builder)) {
  throw "9588 BDA SDK not found at $SdkRoot"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

if ([string]::IsNullOrWhiteSpace($GamePath)) {
  if (Test-Path -LiteralPath $ProjectGame) {
    $GamePath = $ProjectGame
  } else {
    $GamePath = $OriginalGame
  }
}
if (-not (Test-Path -LiteralPath $GamePath)) {
  throw "Authorized 9288S D300 game not found. Pass -GamePath or copy it to $ProjectGame"
}

$GameBytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $GamePath))
$BuilderText = [System.Text.StringBuilder]::new($GameBytes.Length * 6)
[void]$BuilderText.AppendLine("/* Generated locally; do not commit proprietary game bytes. */")
[void]$BuilderText.AppendLine("static const u8 k_embedded_game[] __attribute__((aligned(4))) = {")
for ($Index = 0; $Index -lt $GameBytes.Length; $Index++) {
  if (($Index % 12) -eq 0) {
    [void]$BuilderText.Append("    ")
  }
  [void]$BuilderText.AppendFormat("0x{0:x2},", $GameBytes[$Index])
  if (($Index % 12) -eq 11 -or $Index -eq $GameBytes.Length - 1) {
    [void]$BuilderText.AppendLine()
  } else {
    [void]$BuilderText.Append(" ")
  }
}
[void]$BuilderText.AppendLine("};")
[void]$BuilderText.AppendLine("#define K_EMBEDDED_GAME_SIZE ((u32)sizeof(k_embedded_game))")
[System.IO.File]::WriteAllText(
  $GeneratedGame,
  $BuilderText.ToString(),
  [System.Text.Encoding]::ASCII
)

python $Builder `
  $Source `
  --no-template `
  --title "9288SCompat" `
  --category 9 `
  -o $Output
if ($LASTEXITCODE -ne 0) {
  throw "BDA build failed with exit code $LASTEXITCODE"
}

Write-Output "embedded game: $GamePath ($($GameBytes.Length) bytes)"
Write-Output "built: $Output"
