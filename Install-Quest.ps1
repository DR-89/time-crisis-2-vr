param([string]$Serial,[string]$Apk)
$ErrorActionPreference='Stop'
if(!$Apk){$Apk=Join-Path $PSScriptRoot 'artifacts\bundled\TimeCrisis2VR-with-ROM.apk'}
if(!(Test-Path -LiteralPath $apk)){throw 'APK missing. Run Build-Quest.ps1 first.'}
$adb=if(Get-Command adb -ErrorAction SilentlyContinue){(Get-Command adb).Source}else{'C:\platform-tools\adb.exe'}
$targetArgs=@()
if($Serial){$targetArgs=@('-s',$Serial)}
& $adb @targetArgs get-state
if($LASTEXITCODE -ne 0){throw 'Connect Quest via USB, enable developer mode and accept the USB debugging prompt in the headset.'}
& $adb @targetArgs install -r $apk
if($LASTEXITCODE -ne 0){throw 'APK installation failed.'}
$activity=(& $adb @targetArgs shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -p org.timecrisis2.arcadevr | Select-Object -Last 1).Trim()
if($LASTEXITCODE -ne 0 -or $activity -notmatch '^org\.timecrisis2\.arcadevr/\.(MainActivity|LauncherActivity)$'){throw 'Cannot resolve the app launcher.'}
& $adb @targetArgs shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n $activity
if($LASTEXITCODE -ne 0){throw 'APK launch failed.'}
