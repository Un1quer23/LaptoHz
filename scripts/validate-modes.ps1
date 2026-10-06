# ThinkBook-specific regression: requires validated physical 60/240Hz modes, a 240Hz AC
# target and stable AC power. Changes the real rate; finishes in automatic mode.
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$programPath = Join-Path $projectDirectory 'dist\LaptoHz\LaptoHz.exe'
$diagnosticPath = Join-Path $projectDirectory 'build\modes-diagnostic.json'
$settingsPath = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'RefreshRateSwitcher\settings.ini'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class RrsModeValidation {
    public delegate bool WindowProc(IntPtr window, IntPtr parameter);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct MenuBar { public uint size; public Rect rect; public IntPtr menu,window; public uint flags; }
    [DllImport("user32.dll")] static extern bool EnumThreadWindows(uint thread, WindowProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool GetMenuBarInfo(IntPtr window, int obj, int item, ref MenuBar info);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr menu, uint item, uint flags);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetMenuStringW(IntPtr menu, uint item, StringBuilder text, int size, uint flags);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr menu, int position);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint key, uint type);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string caption);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern uint GetPrivateProfileString(string section, string key, string value, StringBuilder text, uint size, string path);
    public static string Mode(string path) {
        var text = new StringBuilder(32); GetPrivateProfileString("App","Mode","auto",text,32,path); return text.ToString();
    }
    public static string Label(IntPtr menu, uint item) {
        var text = new StringBuilder(512); GetMenuStringW(menu,item,text,512,0); return text.ToString();
    }
    public static IntPtr Menu(IntPtr owner) {
        uint process; uint thread = GetWindowThreadProcessId(owner,out process);
        IntPtr result = IntPtr.Zero;
        EnumThreadWindows(thread,delegate(IntPtr window,IntPtr parameter) {
            var info = new MenuBar(); info.size = (uint)Marshal.SizeOf(typeof(MenuBar));
            if (IsWindowVisible(window) && GetMenuBarInfo(window,-4,0,ref info) && info.menu != IntPtr.Zero) { result=info.menu; return false; }
            return true;
        },IntPtr.Zero); return result;
    }
    public static void Key(IntPtr owner, uint key) {
        long data = ((long)MapVirtualKey(key,0)<<16) | 1;
        PostMessageW(owner,0x100,new UIntPtr(key),new IntPtr(data));
        PostMessageW(owner,0x101,new UIntPtr(key),new IntPtr(data | 0xC0000000L));
    }
}
'@
function Invoke-Control([string[]]$arguments, [int]$expected = 0) {
    $commandProcess = Start-Process -FilePath $programPath -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($commandProcess.ExitCode -ne $expected) { throw ('Command failed: ' + ($arguments -join ' ')) }
}
function Read-Diagnostic {
    Invoke-Control @('--diagnose','--output',('"' + $diagnosticPath + '"'))
    return Get-Content -LiteralPath $diagnosticPath -Raw | ConvertFrom-Json
}
function Wait-Rate([int]$rate) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $diagnostic = Read-Diagnostic
        $signalMatches = [Math]::Abs($diagnostic.physicalHz - $rate) -lt 0.5
        if ($diagnostic.nominalHz -eq $rate -and $signalMatches) { return }
        Start-Sleep -Milliseconds 150
    } while ($timer.ElapsedMilliseconds -lt 6000)
    throw ('The expected refresh rate was not reached: ' + $rate)
}
function Check-Mode([string]$mode) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        if ([RrsModeValidation]::Mode($settingsPath) -eq $mode) { return }
        Start-Sleep -Milliseconds 50
    } while ($timer.ElapsedMilliseconds -lt 2000)
    throw ('Mode was not saved: ' + $mode)
}
function Check-Host([Diagnostics.Process]$appProcess) {
    $hostWindow = [RrsModeValidation]::FindWindowW('RefreshRateSwitcher.Host.v1','LaptoHz')
    [uint32]$actualProcess = 0
    [void][RrsModeValidation]::GetWindowThreadProcessId($hostWindow,[ref]$actualProcess)
    if ($hostWindow -eq [IntPtr]::Zero -or $actualProcess -ne $appProcess.Id) { throw 'The host does not match the packaged application.' }
    return $hostWindow
}
function Check-NoConfirmation {
    $popupWindow = [RrsModeValidation]::FindWindowW('RefreshRateSwitcher.Notification.v1','刷新率切换确认')
    if ($popupWindow -ne [IntPtr]::Zero -and [RrsModeValidation]::IsWindowVisible($popupWindow)) { throw 'Unexpected power confirmation.' }
}
function Select-MenuCommand([IntPtr]$hostWindow, [IntPtr]$menuHandle, [uint32]$targetCommand) {
    if (([RrsModeValidation]::GetMenuState($menuHandle,$targetCommand,0) -band 3) -ne 0) { throw 'The requested menu action is disabled.' }
    [RrsModeValidation]::Key($hostWindow,0x24)
    Start-Sleep -Milliseconds 40
    for ($step = 0; $step -lt ([RrsModeValidation]::GetMenuItemCount($menuHandle) + 2); $step++) {
        if (([RrsModeValidation]::GetMenuState($menuHandle,$targetCommand,0) -band 0x80) -ne 0) {
            [RrsModeValidation]::Key($hostWindow,0x0D)
            Start-Sleep -Milliseconds 60
            return
        }
        [RrsModeValidation]::Key($hostWindow,0x28)
        Start-Sleep -Milliseconds 40
    }
    [RrsModeValidation]::Key($hostWindow,0x1B)
    throw 'Native menu keyboard navigation did not reach the target.'
}
function Select-TrayRate([Diagnostics.Process]$appProcess, [int]$rate, [int]$gesture = 0x400) {
    $hostWindow = Check-Host $appProcess
    $firstGesture = if ($gesture -eq 0x7B) { 0x205 } else { $gesture }
    [void][RrsModeValidation]::PostMessageW($hostWindow,0x8001,[UIntPtr]::Zero,[IntPtr]::new(0x10000 -bor $firstGesture))
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $menuHandle = [RrsModeValidation]::Menu($hostWindow)
        if ($menuHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 20
    } while ($timer.ElapsedMilliseconds -lt 3000)
    if ($menuHandle -eq [IntPtr]::Zero) { throw 'The unified tray menu did not appear.' }
    foreach ($choice in @($initial.supportedHz)) {
        $command = Find-RateCommand $menuHandle $choice
        if (([RrsModeValidation]::GetMenuState($menuHandle,$command,0) -band 8) -ne 0) { throw 'A refresh-rate action must not carry a mode-selection check.' }
        $expectedLabel = [string]$choice + 'Hz'
        if (-not [RrsModeValidation]::Label($menuHandle,$command).StartsWith($expectedLabel)) { throw 'Unexpected refresh-rate action label.' }
    }
    if ($gesture -eq 0x7B) {
        # One right click may report button release and then context-menu activation.
        # Check the real menu loop rather than testing the callbacks separately.
        [void][RrsModeValidation]::PostMessageW($hostWindow,0x8001,[UIntPtr]::Zero,[IntPtr]::new(0x10000 -bor 0x7B))
        Start-Sleep -Milliseconds 250
        if ([RrsModeValidation]::Menu($hostWindow) -ne $menuHandle) { throw 'The second right-click notification closed the menu.' }
        [void][RrsModeValidation]::PostMessageW($hostWindow,0x8001,[UIntPtr]::Zero,[IntPtr]::new(0x10000 -bor 0x205))
        [void][RrsModeValidation]::PostMessageW($hostWindow,0x8001,[UIntPtr]::Zero,[IntPtr]::new(0x10000 -bor 0x7B))
        Start-Sleep -Milliseconds 250
        if ([RrsModeValidation]::Menu($hostWindow) -ne $menuHandle) { throw 'Repeated right-click notifications replaced or closed the menu.' }
    }
    if ([RrsModeValidation]::Mode($settingsPath) -ne 'manual') {
        foreach ($choice in @($initial.supportedHz)) {
            $command = Find-RateCommand $menuHandle $choice
            if (([RrsModeValidation]::GetMenuState($menuHandle,$command,0) -band 3) -eq 0) { throw 'Non-manual modes must disable manual rate actions.' }
        }
        if (-not [RrsModeValidation]::Label($menuHandle,106).Contains('请先选择手动模式')) { throw 'The manual prerequisite is not explained.' }
        $rateBeforeManual = (Read-Diagnostic).nominalHz
        Select-MenuCommand $hostWindow $menuHandle 112
        Check-Mode 'manual'
        $timer.Restart()
        do {
            $menuHandle = [RrsModeValidation]::Menu($hostWindow)
            if ($menuHandle -ne [IntPtr]::Zero -and ([RrsModeValidation]::GetMenuState($menuHandle,112,0) -band 8) -ne 0) { break }
            Start-Sleep -Milliseconds 20
        } while ($timer.ElapsedMilliseconds -lt 3000)
        if ($menuHandle -eq [IntPtr]::Zero -or ([RrsModeValidation]::GetMenuState($menuHandle,112,0) -band 8) -eq 0) { throw 'The menu did not continue after entering manual mode.' }
        Wait-Rate $rateBeforeManual
    }
    $targetCommand = Find-RateCommand $menuHandle $rate
    # Hardware readback can become visible before the worker's result updates
    # the continued menu. Wait for its current marker or enabled action.
    $timer.Restart()
    do {
        $targetState = [RrsModeValidation]::GetMenuState($menuHandle,$targetCommand,0)
        $targetLabel = [RrsModeValidation]::Label($menuHandle,$targetCommand)
        if (($targetState -band 3) -eq 0 -or $targetLabel.Contains('（当前）')) { break }
        Start-Sleep -Milliseconds 40
    } while ($timer.ElapsedMilliseconds -lt 6000)
    if (([RrsModeValidation]::GetMenuState($menuHandle,$targetCommand,0) -band 3) -ne 0) {
        if (-not [RrsModeValidation]::Label($menuHandle,$targetCommand).Contains('（当前）') -or (Read-Diagnostic).nominalHz -ne $rate) {
            throw ('The requested noncurrent rate is unexpectedly disabled: ' + $rate + '; label=' + $targetLabel + '; flags=' + $targetState)
        }
        [RrsModeValidation]::Key($hostWindow,0x1B)
        Start-Sleep -Milliseconds 200
        if ([RrsModeValidation]::Menu($hostWindow) -ne [IntPtr]::Zero) { throw 'The closed continued menu reopened.' }
        return
    }
    Select-MenuCommand $hostWindow $menuHandle $targetCommand
}
function Find-RateCommand([IntPtr]$menuHandle, [int]$rate) {
    for ($position = 0; $position -lt [RrsModeValidation]::GetMenuItemCount($menuHandle); $position++) {
        $command = [RrsModeValidation]::GetMenuItemID($menuHandle,$position)
        if ($command -ne [uint32]::MaxValue -and $command -ne 100 -and
            [RrsModeValidation]::Label($menuHandle,$command).StartsWith(([string]$rate + 'Hz'))) { return $command }
    }
    throw ('The manual rate is missing from the menu: ' + $rate)
}
$instances = @(Get-Process -Name LaptoHz -ErrorAction SilentlyContinue | Where-Object Path -EQ $programPath)
if ($instances.Count -ne 1) { throw 'Expected one running packaged application.' }
$appProcess = $instances[0]
$initial = Read-Diagnostic
if ($initial.power -ne '外部供电' -or $initial.version -ne '0.3.0-beta.4' -or $initial.validation60 -ne 0 -or
    $initial.validation240 -ne 0 -or ($initial.supportedHz -join ',') -ne '60,240' -or
    $initial.resolvedTargets.AC.hz -ne 240 -or -not $initial.resolvedTargets.AC.available) {
    throw 'This ThinkBook regression requires v0.3.0-beta.4, physical 60/240Hz and a valid 240Hz AC target on stable AC power.'
}
$startupShortcut = Join-Path ([Environment]::GetFolderPath('Startup')) 'LaptoHz.lnk'
$startupBefore = if (Test-Path -LiteralPath $startupShortcut) { (Get-FileHash -LiteralPath $startupShortcut).Hash } else { $null }
$legacyBefore = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run').RefreshRateSwitcher
Invoke-Control @('--mode','auto'); Check-Mode 'auto'; Wait-Rate 240
Invoke-Control @('--switch','60') 4; Check-Mode 'auto'; Wait-Rate 240
Select-TrayRate $appProcess 60; Check-Mode 'manual'; Wait-Rate 60
$hostWindow = Check-Host $appProcess
[void][RrsModeValidation]::PostMessageW($hostWindow,0x218,[UIntPtr]::new(0xA),[IntPtr]::Zero)
[void][RrsModeValidation]::PostMessageW($hostWindow,0x218,[UIntPtr]::new(0x12),[IntPtr]::Zero)
Start-Sleep -Milliseconds 1500
Wait-Rate 60; Check-NoConfirmation
Invoke-Control @('--exit')
if (-not $appProcess.WaitForExit(8000)) { throw 'Application did not exit.' }
$appProcess = Start-Process -FilePath $programPath -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 1000
Check-Mode 'manual'; Wait-Rate 60
Invoke-Control @('--mode','confirm'); Check-Mode 'confirm'
Invoke-Control @('--switch','240') 4; Check-Mode 'confirm'; Wait-Rate 60
$hostWindow = Check-Host $appProcess
[void][RrsModeValidation]::PostMessageW($hostWindow,0x218,[UIntPtr]::new(0xA),[IntPtr]::Zero)
[void][RrsModeValidation]::PostMessageW($hostWindow,0x218,[UIntPtr]::new(0x12),[IntPtr]::Zero)
Start-Sleep -Milliseconds 1500
Wait-Rate 60; Check-NoConfirmation
Select-TrayRate $appProcess 240 0x7B; Check-Mode 'manual'; Wait-Rate 240
Select-TrayRate $appProcess 60; Check-Mode 'manual'; Wait-Rate 60
Invoke-Control @('--switch','60'); Check-Mode 'manual'; Wait-Rate 60
Invoke-Control @('--mode','auto'); Check-Mode 'auto'; Wait-Rate 240
Select-TrayRate $appProcess 240; Check-Mode 'manual'; Wait-Rate 240
Invoke-Control @('--switch','240'); Check-Mode 'manual'; Wait-Rate 240
foreach ($filtered in @('30','48','120')) { Invoke-Control @('--switch',$filtered) 4 }
Wait-Rate 240
Invoke-Control @('--switch','999999') 4
Invoke-Control @('--switch','60.0') 2
Invoke-Control @('--mode','invalid') 2
Check-Mode 'manual'
Invoke-Control @('--mode','auto'); Check-Mode 'auto'; Wait-Rate 240
$startupAfter = if (Test-Path -LiteralPath $startupShortcut) { (Get-FileHash -LiteralPath $startupShortcut).Hash } else { $null }
$legacyAfter = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run').RefreshRateSwitcher
if ($startupBefore -ne $startupAfter -or $legacyBefore -ne $legacyAfter) { throw 'Mode validation changed the startup preference.' }
[ordered]@{
    Version='0.3.0-beta.4'
    AllValidatedManualRatesListed=$true
    ArbitrarySwitchCommandPassed=$true
    VirtualRefreshRatesRejected=$true
    UnifiedMenuLeftAndRight=$true
    RefreshRateActionsAreNotChecked=$true
    RightClickSequenceKeepsMenu=$true
    MenuKeyboardSelection=$true
    SwitchRequiresExplicitManualMode=$true
    MenuContinuesAfterManualMode=$true
    ManualModeSurvivedRestart=$true
    ManualModeSurvivedPowerAndResumeNotifications=$true
    SameSourceConfirmationDidNotPrompt=$true
    EnteringManualPreservedCurrentRate=$true
    CurrentRateIsDisabled=$true
    InvalidArgumentsRejected=$true
    StartupPreferencePreserved=$true
    FinalMode=[RrsModeValidation]::Mode($settingsPath)
    FinalNominalHz=(Read-Diagnostic).nominalHz
    RunningPid=$appProcess.Id
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $projectDirectory 'build\modes-validation.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $projectDirectory 'build\modes-validation.json')
