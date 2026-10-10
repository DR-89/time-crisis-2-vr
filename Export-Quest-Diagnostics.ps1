param([string]$Serial,[string]$Output,[string]$Adb)
$ErrorActionPreference='Stop'
if(!$Adb){$Adb=if(Get-Command adb -ErrorAction SilentlyContinue){(Get-Command adb).Source}else{'C:\platform-tools\adb.exe'}}
if(!(Test-Path -LiteralPath $Adb)){throw 'ADB not found. Install Android platform-tools or pass -Adb with the path to adb.exe.'}
function Read-Adb([string[]]$CommandArgs){
    # A missing optional previous-session file is normal. Preserve native exit
    # status without PowerShell 5 turning stderr into a terminating exception.
    $oldPreference=$ErrorActionPreference
    try{
        $ErrorActionPreference='Continue'
        $lines=@(& $Adb @CommandArgs 2>$null)
        [pscustomobject]@{Lines=$lines;Code=$LASTEXITCODE}
    }finally{$ErrorActionPreference=$oldPreference}
}
if(!$Serial){
    $devices=Read-Adb @('devices','-l')
    if($devices.Code -ne 0){throw 'ADB could not list devices.'}
    $quests=@($devices.Lines | ForEach-Object {if($_ -match '^(\S+)\s+device\s+.*\bmodel:Quest(?:_\S+)?(?:\s|$)'){$Matches[1]}})
    if($quests.Count -ne 1){throw 'Connect exactly one Quest and allow USB debugging, or select it with -Serial. Other Android devices are not selected automatically.'}
    $Serial=$quests[0]
}
$targetArgs=@('-s',$Serial)
$access=Read-Adb ($targetArgs+@('shell','run-as','org.timecrisis2.arcadevr','id'))
if($access.Code -ne 0){throw 'Cannot read Time Crisis II VR on this device. Check the selected Quest, installation and USB debugging permission.'}
if(!$Output){$Output=Join-Path $PSScriptRoot ('artifacts\diagnostics-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))}
$Output=[IO.Path]::GetFullPath($Output)
$archive=$Output+'.zip'
if((Test-Path -LiteralPath $archive) -or ((Test-Path -LiteralPath $Output) -and (Get-ChildItem -LiteralPath $Output -Force | Select-Object -First 1))){throw 'Output already contains a report. Choose a new -Output directory.'}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
$copied=@();$missing=@()
foreach($name in @('timecrisis2-vr.log','timecrisis2-vr-previous.log','tc2_last.rec','tc2_prev.rec','quest-options.cfg')){
    $result=Read-Adb ($targetArgs+@('shell','run-as','org.timecrisis2.arcadevr','cat',"files/$name"))
    if($result.Code -eq 0){
        # Keep input recordings as plain UTF-8 text without a Windows BOM.
        [IO.File]::WriteAllText((Join-Path $Output $name),($result.Lines -join "`n")+"`n",$utf8)
        $copied+=$name
    }else{$missing+=$name}
}
foreach($query in @(@('exit-info.txt','shell','dumpsys','activity','exit-info','org.timecrisis2.arcadevr'),@('package.txt','shell','dumpsys','package','org.timecrisis2.arcadevr'))){
    $result=Read-Adb ($targetArgs+$query[1..($query.Count-1)])
    if($result.Code -eq 0){[IO.File]::WriteAllText((Join-Path $Output $query[0]),($result.Lines -join "`n")+"`n",$utf8)}else{$missing+=$query[0]}
}
if(!($copied -contains 'timecrisis2-vr.log') -and !($copied -contains 'timecrisis2-vr-previous.log')){throw "No game log could be read. Partial diagnostics remain in $Output; no ZIP was created."}
$report=@('Time Crisis II VR diagnostic export','Collected: '+(Get-Date -Format o),'Copied: '+($copied -join ', '),'Unavailable: '+($missing -join ', '),'Input recordings contain arcade controls and gun coordinates, not video/audio/head poses.','Nothing has been uploaded. Review the files before sharing them.') -join "`n"
[IO.File]::WriteAllText((Join-Path $Output 'report.txt'),$report+"`n",$utf8)
Compress-Archive -LiteralPath $Output -DestinationPath $archive
Write-Host "Diagnostics saved to $Output"
Write-Host "Shareable ZIP: $archive"
