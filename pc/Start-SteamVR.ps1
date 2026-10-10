$ErrorActionPreference = 'Stop'
try {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath
    $runtime = Join-Path $steam 'steamapps\common\SteamVR\steamxr_win64.json'
    if (!(Test-Path -LiteralPath $runtime)) {
        throw 'SteamVR was not found in the standard Steam library. Start your VR software and use Play VR.cmd, or set XR_RUNTIME_JSON to your SteamVR runtime manifest.'
    }
    $env:XR_RUNTIME_JSON = $runtime
    Set-Location -LiteralPath $PSScriptRoot
    & (Join-Path $PSScriptRoot 'TimeCrisis2VR.exe')
} catch {
    Write-Host $_.Exception.Message
    Read-Host 'Press Enter to close'
}
