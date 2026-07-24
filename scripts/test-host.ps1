$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build\host-tests"
$TestExe = Join-Path $BuildDir "test-runtime.exe"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

python -m unittest discover -s (Join-Path $ProjectRoot "tests") -p "test_*.py"
if ($LASTEXITCODE -ne 0) {
  throw "Python tests failed with exit code $LASTEXITCODE"
}

gcc `
  -std=c11 `
  -Wall `
  -Wextra `
  -Werror `
  -I (Join-Path $ProjectRoot "runtime\include") `
  (Join-Path $ProjectRoot "runtime\src\d300.c") `
  (Join-Path $ProjectRoot "runtime\src\c33vm.c") `
  (Join-Path $ProjectRoot "runtime\src\compat_api.c") `
  (Join-Path $ProjectRoot "runtime\src\compat_fs.c") `
  (Join-Path $ProjectRoot "tests\test_runtime.c") `
  -o $TestExe
if ($LASTEXITCODE -ne 0) {
  throw "C host test build failed with exit code $LASTEXITCODE"
}

& $TestExe
if ($LASTEXITCODE -ne 0) {
  throw "C host tests failed with exit code $LASTEXITCODE"
}
