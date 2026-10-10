param([string]$Rom=(Join-Path $env:USERPROFILE 'Downloads\timecrs2.zip'),[int]$Jobs=2)
$ErrorActionPreference='Stop'
$py=if(Get-Command python -ErrorAction SilentlyContinue){(Get-Command python).Source}else{throw 'Install Python 3 and add it to PATH.'}
& $py -X utf8 (Join-Path $PSScriptRoot 'tools\build_pc.py') --rom $Rom --jobs $Jobs
if($LASTEXITCODE -ne 0){throw 'PC build failed.'}
