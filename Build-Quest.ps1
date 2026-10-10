param([string]$Rom = "$env:USERPROFILE\Downloads\timecrs2.zip", [int]$Jobs=2, [switch]$BundleRoms)
$ErrorActionPreference='Stop'
$bundledPython=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$python=if(Test-Path -LiteralPath $bundledPython){$bundledPython}else{'python'}
$extra=@()
if($BundleRoms){$extra+='--bundle-roms'}
& $python -X utf8 (Join-Path $PSScriptRoot 'tools\build.py') --rom $Rom --jobs $Jobs @extra
if($LASTEXITCODE -ne 0){throw "Quest build failed ($LASTEXITCODE)"}
