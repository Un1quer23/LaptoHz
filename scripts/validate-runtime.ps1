# Run after launching the packaged application. Leaves it running with startup enabled.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$releaseExe = Join-Path $projectRoot 'dist\LaptoHz\LaptoHz.exe'
$startupShortcut = Join-Path ([Environment]::GetFolderPath('Startup')) 'LaptoHz.lnk'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RrsValidationNative {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string caption);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
}
'@
function Read-StartupEntry {
    if (-not (Test-Path -LiteralPath $startupShortcut -PathType Leaf)) { return $null }
    $shell = New-Object -ComObject WScript.Shell
    try {
        $link = $shell.CreateShortcut($startupShortcut)
        return [ordered]@{ Path=$startupShortcut; Target=$link.TargetPath; Arguments=$link.Arguments; WorkingDirectory=$link.WorkingDirectory }
    } finally {
        if ($link) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($link) }
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
    }
}
function Read-LegacyEntry {
    return (Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -ErrorAction SilentlyContinue).RefreshRateSwitcher
}
function Check-StartupEntry {
    $entry = Read-StartupEntry
    if (-not $entry -or $entry.Target -ne $releaseExe -or $entry.Arguments -ne '--startup' -or
        $entry.WorkingDirectory -ne (Split-Path -Parent $releaseExe) -or (Read-LegacyEntry)) {
        throw 'The standard startup shortcut is missing, incorrect, or duplicated by a legacy Run entry.'
    }
}
function Toggle-Startup([System.Diagnostics.Process]$process) {
    $window = [RrsValidationNative]::FindWindowW('RefreshRateSwitcher.Host.v1','LaptoHz')
    [uint32]$windowProcess = 0
    [void][RrsValidationNative]::GetWindowThreadProcessId($window,[ref]$windowProcess)
    if ($window -eq [IntPtr]::Zero -or $windowProcess -ne $process.Id) { throw 'Test host does not match the packaged process.' }
    if (-not [RrsValidationNative]::PostMessageW($window,0x111,[UIntPtr]::new(102),[IntPtr]::Zero)) { throw 'Menu command failed.' }
}
$instances = @(Get-Process -Name LaptoHz -ErrorAction SilentlyContinue | Where-Object Path -EQ $releaseExe)
if ($instances.Count -ne 1) { throw 'Expected one running packaged application.' }
$appProcess = $instances[0]
Check-StartupEntry
Toggle-Startup $appProcess
Start-Sleep -Milliseconds 500
if ((Read-StartupEntry) -or (Read-LegacyEntry)) { throw 'Startup menu did not remove its owned startup entries.' }
$exitCommand = Start-Process -FilePath $releaseExe -ArgumentList '--exit' -WindowStyle Hidden -Wait -PassThru
if ($exitCommand.ExitCode -ne 0 -or -not $appProcess.WaitForExit(8000)) { throw 'Application failed to exit.' }
$appProcess = Start-Process -FilePath $releaseExe -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 1500
$appProcess.Refresh()
if ($appProcess.HasExited) { throw 'Application failed to restart.' }
if ((Read-StartupEntry) -or (Read-LegacyEntry)) { throw 'Manual restart re-enabled a disabled startup preference.' }
Toggle-Startup $appProcess
Start-Sleep -Milliseconds 500
Check-StartupEntry
$exitCommand = Start-Process -FilePath $releaseExe -ArgumentList '--exit' -WindowStyle Hidden -Wait -PassThru
if ($exitCommand.ExitCode -ne 0 -or -not $appProcess.WaitForExit(8000)) { throw 'Application failed to exit before the shortcut launch.' }
$shortcutLaunchTime = Get-Date
# ShellExecute resolves the same link Windows uses at sign-in. This verifies
# launch plumbing; it deliberately does not sign out or reboot the computer.
$shortcutProcess = Start-Process -FilePath $startupShortcut -WorkingDirectory (Join-Path $env:WINDIR 'System32') -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 1500
$instances = @(Get-Process -Name LaptoHz -ErrorAction SilentlyContinue | Where-Object Path -EQ $releaseExe)
if ($instances.Count -ne 1) { throw 'The startup shortcut did not leave one running packaged application.' }
$appProcess = $instances[0]
$hostWindow = [RrsValidationNative]::FindWindowW('RefreshRateSwitcher.Host.v1','LaptoHz')
[uint32]$hostProcess = 0
[void][RrsValidationNative]::GetWindowThreadProcessId($hostWindow,[ref]$hostProcess)
if ($hostWindow -eq [IntPtr]::Zero -or $hostProcess -ne $appProcess.Id) { throw 'The startup launch has no initialized tray host.' }
$logLines = Get-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'RefreshRateSwitcher\switcher.log') -Tail 10
$runtimeVersion=(Get-Item -LiteralPath $releaseExe).VersionInfo.ProductVersion
if (-not ($logLines -match ('准备启动 v'+[regex]::Escape($runtimeVersion)+' · 登录自启'))) { throw 'The shortcut did not pass the startup argument.' }
Check-StartupEntry
$enumeratedStartup = @(Get-CimInstance Win32_StartupCommand | Where-Object { $_.Name -eq 'LaptoHz' -and $_.Location -eq 'Startup' })
if ($enumeratedStartup.Count -ne 1) { throw 'Windows startup enumeration does not recognize the shortcut.' }
# Allow the notification timer and startup enumeration to settle before sampling idle cost.
Start-Sleep -Seconds 10
$appProcess.Refresh()
$cpuBefore = $appProcess.TotalProcessorTime.TotalMilliseconds
$timer = [Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds 10
$appProcess.Refresh()
$timer.Stop()
$metrics = [ordered]@{
    StartupTogglePassed=$true
    DisabledStartupSurvivedRestart=$true
    StartupShortcutLaunched=$true
    StartupArgumentVerified=$true
    WindowsStartupEnumerationPassed=$true
    TrayHostInitialized=$true
    WindowsRebootTested=$false
    RunningPid=$appProcess.Id
    StartupRegistration=(Read-StartupEntry)
    Version=(Get-Item -LiteralPath $releaseExe).VersionInfo.ProductVersion
    ExecutableBytes=(Get-Item -LiteralPath $releaseExe).Length
    IdleWorkingSetMiB=[math]::Round($appProcess.WorkingSet64/1MB,2)
    IdleCpuMilliseconds=[math]::Round($appProcess.TotalProcessorTime.TotalMilliseconds-$cpuBefore,3)
    SampleSeconds=[math]::Round($timer.Elapsed.TotalSeconds,3)
}
$json = $metrics | ConvertTo-Json
$json | Set-Content -LiteralPath (Join-Path $projectRoot 'build\runtime-metrics.json') -Encoding utf8
Write-Output $json
