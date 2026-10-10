param(
    [Parameter(Mandatory=$true)][string]$Rom,
    [string]$Apk,
    [string]$Serial
)
$ErrorActionPreference='Stop'
if(!(Test-Path -LiteralPath $Rom -PathType Leaf)){throw 'ROM ZIP not found.'}
$adb=if(Get-Command adb -ErrorAction SilentlyContinue){(Get-Command adb).Source}elseif(Test-Path -LiteralPath 'C:\platform-tools\adb.exe'){'C:\platform-tools\adb.exe'}else{throw 'Install Android platform-tools and add adb to PATH.'}
$targetArgs=@()
if($Serial){$targetArgs=@('-s',$Serial)}
function Invoke-Adb {
    & $adb @targetArgs @args
    if($LASTEXITCODE -ne 0){throw "ADB failed: $($args[0])"}
}
Invoke-Adb get-state
if($Apk){
    if(!(Test-Path -LiteralPath $Apk -PathType Leaf)){throw 'APK not found.'}
    Invoke-Adb install -r $Apk
}
Invoke-Adb shell am force-stop org.timecrisis2.arcadevr
$destination='/sdcard/Android/data/org.timecrisis2.arcadevr/files'
Invoke-Adb shell mkdir -p $destination
Invoke-Adb push $Rom "$destination/timecrs2.zip"
$activity=(Invoke-Adb shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -p org.timecrisis2.arcadevr | Select-Object -Last 1).Trim()
if($activity -notmatch '^org\.timecrisis2\.arcadevr/\.(MainActivity|LauncherActivity)$'){throw 'Cannot resolve the app launcher.'}
Invoke-Adb shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n $activity
Write-Host 'The app now verifies and imports your own ROM set. No ROM files are downloaded.'
