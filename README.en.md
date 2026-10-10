# LaptoHz

**Languages:** [简体中文](README.md) | English

A Windows 11 x64 laptop internal-display refresh rate utility with automatic switching based on power source, confirmation before switching, and manual rate selection.

LaptoHz supports Simplified Chinese and English, following the Windows display language by default or a preference chosen in the tray menu. It detects available refresh rates from the internal display's physical capabilities reported by Windows, verifies switch results and recovery, and resizes popups when display scaling changes. Tested devices and outstanding checks are listed in the [compatibility notes](docs/laptop-compatibility.md) and [validation record](VALIDATION.md).

| Mode | Behavior |
|---|---|
| Automatic | Switches according to power-source rules and checks the rate at startup, on wake, and when display capabilities change |
| Confirmation | Asks before switching after an actual power-source change |
| Manual | Lets you choose a rate; power-source changes and wake do not override it |

The default AC target is the **highest available rate**. On battery, the default prefers **60Hz**, then 59Hz. If neither exists, it selects the lowest available rate at or above 60Hz; if all rates are below 60Hz, it selects the highest. Other detected and validated physical rates can also be selected manually or configured as targets.

## Usage

Download `LaptoHz-0.4.0-win-x64.zip` from the [GitHub Release](https://github.com/Un1quer23/LaptoHz/releases/tag/v0.4.0), extract it, and run `LaptoHz/LaptoHz.exe`. The release includes a `.sha256` checksum for the ZIP; `SHA256SUMS.txt` inside the package lists the SHA256 of each file.

Put the EXE in a fixed directory and double-click it. Administrator privileges and an additional C++ runtime are not required. The first run enables startup at sign-in for the current user by default. The tray icon may be in the taskbar's hidden-icons area.

**Simplified Chinese and English are supported.** By default it follows the Windows display language: Chinese Windows uses Simplified Chinese; other display languages use English. The tray menu's **Language / 语言** submenu offers **Follow Windows / 跟随系统**, **简体中文**, and **English**. Changes apply immediately and are remembered for the next launch. Existing confirmation suggestions stay open when changing language; the operating mode and refresh rate targets are preserved, and selecting a language does not submit a display change.

**Left-click and right-click open the same tray menu.** Its header shows the current refresh rate, power source, and mode. When desktop and signal rates differ, both are shown, for example `30Hz (signal 60Hz)`. Use the arrow keys, Enter, and Esc to navigate; clicking outside closes the menu. Menus, confirmation windows, and result notices follow the Windows app light/dark theme and use system colors in high-contrast mode.

For a first run:

1. Left-click or right-click the tray icon and check the refresh rate, power source, and mode in the header.
2. Hover over Automatic, Confirmation, or Manual mode for about **0.5 seconds** to show an explanation beside the menu. Highlighting a mode with the arrow keys also shows the explanation. Moving to another item, leaving the menu, or closing it hides the hint.
3. Choose Automatic mode to switch according to the power source, or Confirmation mode to decide after each power-source change. Both use the AC and battery targets configured in the menu.
4. To switch manually, first select Manual mode, then choose an available rate below it. A disabled rate marked “current” is already active.

| Mode hint | Explanation |
|---|---|
| Automatic | Switches to the AC/battery target; checks the rate when entering this mode, at startup, and on wake |
| Confirmation | Asks after a power-source change and switches only after confirmation; the prompt stays open until answered, and Keep current or × dismisses that suggestion |
| Manual | Select this mode first, then choose a rate below; power-source changes and wake do not override the choice |

Hovering or highlighting with the arrow keys only shows an explanation. Clicking a mode or pressing Enter selects it. Hints do not steal focus or cover menu items, and their colors follow the menu.

| Menu item | Behavior |
|---|---|
| Automatic / Confirmation / Manual | Mutually exclusive modes, saved across application restarts |
| Plugged-in target | Submenu offering Default: highest available or a currently valid rate |
| Battery target | Submenu offering Default: prefer 60Hz or a currently valid rate |
| Manual refresh rates | Rates supported by the current internal display and validated by the driver, shown directly in the main menu |
| Run automatically at Windows sign-in | Toggles startup for the current user |
| View diagnostic log | Opens local runtime and troubleshooting records in Windows Notepad |
| Language | Follows the Windows display language, or uses Simplified Chinese or English |
| Exit | Exits while keeping the current refresh rate |

**Enter Manual mode before selecting a rate.** Entering it keeps the current display state, cancels earlier power-source tasks and pending suggestions, and leaves the menu open at the same position. The current rate is disabled and marked “current”; an equivalent rate label for a fractional frequency is also disabled and marked “equivalent to current”. Manual rates have no checkmark or radio indicator. The mode group and each target submenu use radio indicators; startup uses a checkmark. The native menu scrolls when many rates are available.

Changing a target does not change the mode. Automatic mode checks it immediately. Confirmation mode cancels the old suggestion and establishes a new power-source baseline, then waits for the next actual power-source change. Manual mode keeps the actual refresh rate. Target editing and repeated rate changes are disabled while a switch is in progress. A selected custom target that becomes unavailable is retained with an explanation; its power-source rule is paused while the other rule can still work. Default targets are resolved again when capabilities change, rather than saving a fixed rate from the current machine.

In Automatic mode, a rate selected through Windows Settings or another tool is kept until an actual power-source change, startup, wake, internal-display recovery, output-route change, or display-capability change. A change in the current Hz alone is not treated as a capability change.

Confirmation mode establishes a power-source baseline at startup, when entering the mode, or when changing a target. Display notifications and wake under the same power source do not create a new suggestion. If the power source changes during sleep, the application asks after wake. It does not create a prompt when the recommended rate is already active. An existing suggestion is updated after another power-source or capability change; stale requests cannot execute.

The confirmation window has no countdown and does not steal focus. It offers Switch to the suggested rate, Keep current, and ×. Keep current or × dismisses only that suggestion and keeps Confirmation mode active. Failures show a reason and allow a retry. The prompt is temporarily hidden during lock, display-off, and sleep. After success, a separate result notice appears. Its × button closes it manually; the lower-right countdown shows `9s → 8s → 7s → 6s → 5s → 4s → 3s → 2s → 1s`, and the notice closes after nine seconds.

Notices and confirmation windows grow to fit wrapped text, including recovery results and observed frequencies. Widths also accommodate titles and action labels, and English popups use Segoe UI. DPI changes resize the window, fonts, and buttons within the monitor work area while preserving focus, the confirmation request, and the original countdown.

## Support boundaries

LaptoHz manages one clearly identifiable internal display. It uses compatible modes with the current resolution, color depth, orientation, and other display parameters. It checks Windows graphics-kernel physical/virtual mode flags and offers physical rates only after `CDS_TEST` validation. It does not enable raw-mode enumeration, create overclocked modes, or create custom display modes. Windows integer Hz labels are used for selection; the signal rate is read separately, including fractional frequencies such as 59.94Hz and 119.88Hz and driver rounding. If physical-capability queries fail, switching pauses and records the reason instead of using an unverified virtual-mode list.

Available rates depend on the current internal display, resolution, and driver capabilities. Virtual desktop rates are excluded as switching targets. If another tool selects such a rate, the status still shows the desktop and signal rates separately. Virtual targets in an existing configuration are retained and marked temporarily unavailable. Successful switching requires both desktop-rate and physical-signal verification. Switching remains disabled while DRR is enabled. See [Microsoft's mode flag documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmdt_displaymode_flags).

A switch changes only the refresh rate. Readback checks the resolution, color depth, orientation, layout, scaling, and advanced color state. If the target readback does not match, the application requests the original mode once, provided the operation has not been cancelled and the original internal-display route and capabilities still match. After a successful recovery request, it reads the state immediately and makes at most four readback attempts, 150ms apart. It checks the original nominal, desktop, and physical-signal rates separately, along with the surrounding display parameters. Actual frequencies use a 0.1Hz tolerance; integer rounding aliases require evidence of a fractional signal. Differences between the original desktop and signal rates are preserved. Unknown or invalid frequencies cannot count as successful recovery. Cancellation or an environment change stops verification and records the reason. The driver may briefly blank the screen during a switch.

An accepted recovery request and verified recovery are recorded separately. Notices, confirmation windows, and logs explain the final recovery result. Even verified recovery still means the target switch failed; the confirmation suggestion remains available for an explicit retry, and the application does not automatically switch back toward the failed target. When recovery cannot be verified, the last observed display state is recorded. Recovery and target driver return codes are stored separately.

An external display in extended-desktop mode is allowed, with its parameters preserved. When only the external display is active, LaptoHz waits for the internal display to return. **Duplicate-display mode, multiple internal displays, remote/inactive sessions, and Windows Dynamic Refresh Rate (DRR) disable switching.** LaptoHz does not turn DRR off automatically; disable it in Windows Advanced display settings. If only one rate is available, the application explains this and avoids repeated switching. ARM64 builds, dedicated Windows 10 acceptance testing, external-display control, and vendor-specific interfaces are not provided.

## Configuration and logs

Files normally live in `%LOCALAPPDATA%\RefreshRateSwitcher`. LaptoHz retains this directory after the rename to preserve existing settings and logs. If the launch environment redirects AppData, files use that environment's application-data directory.

```ini
[App]
StartupInitialized=1
Mode=auto
Language=system

[RefreshRate]
AC=auto
Battery=auto
```

`Mode` accepts `auto`, `confirm`, or `manual`. `AC` and `Battery` accept `auto` or an integer Hz value. Older configurations without target fields use dynamic defaults while preserving mode and startup preferences. An invalid target falls back to the default for that power source only and is logged. A valid integer target that is unavailable is retained rather than silently replaced.

`Language` accepts `system` (follow Windows), `zh-CN` (Simplified Chinese), or `en` (English). Missing or invalid values follow Windows. Language changes preserve other settings. Application messages, error details, and new log entries use the selected language. If Windows lacks a requested system-error translation, the app shows the error code in that language. Existing log entries keep their original text.

If saving fails, the current session uses the selected setting and shows “Settings not saved”, along with a failure notice. On restart, only successfully saved settings can be loaded. Manual mode does not reapply the last manually selected rate at startup.

`switcher.log` records the launch source, mode/target changes, switch requests, target driver return codes, and separate recovery states and return codes. It rotates to `switcher.previous.log`, with each file about 256KiB. Before opening a log in Windows Notepad, the application resolves the actual path from a file handle. Logs stay on the local machine and are not automatically uploaded.

Startup uses `LaptoHz.lnk` in the current user's Startup folder, with the argument `--startup` and the EXE directory as its working directory. Enter `shell:startup` in File Explorer to inspect it. Migration from the old Run entry, Windows disabled-startup choices, and a user's choice to turn startup off are respected. Before moving or deleting the program, disable startup and exit. Testing a launch through the Startup-folder shortcut does not replace testing an actual sign-out/sign-in.

To upgrade from a version named Refresh Rate Switcher, first disable startup in the old application's menu and exit, then run `LaptoHz.exe`. Enable startup again in the new menu if desired. Existing modes and refresh-rate targets are loaded from the original configuration. Old and new versions share a singleton marker to prevent two resident instances.

## Commands and compatibility reports

```powershell
# Read-only diagnostics: no tray instance or startup registration; writes a JSON report
.\LaptoHz.exe --diagnose --output .\diagnostics.json

# Control an existing instance
.\LaptoHz.exe --mode manual
.\LaptoHz.exe --switch 60
.\LaptoHz.exe --mode confirm
.\LaptoHz.exe --mode auto
```

`--switch <integer Hz>` requires Manual mode and a currently available target. Selecting the current rate refreshes the status without applying it again or creating an unnecessary notice. `--pause` selects Manual mode, `--resume` selects Automatic mode, `--status` shows the status, `--exit` exits, and `--version` prints the version.

Use `--diagnose --language en` for English diagnostic descriptions, or `--preview-notification --language en --capture .\english-preview.png` to preview an English result notice. `--language` is accepted only with diagnostics or previews and takes `system`, `zh-CN`, or `en`. It does not change saved settings; choose a resident instance's language through its tray menu. Diagnostic JSON field names, numeric values, and rate logic remain unchanged; readable power and error descriptions follow the language preference.

Control replies have a three-second timeout. Exit codes are: 0 for an adopted mode or accepted switch request; 1 for no running instance, no response, or an unsupported older version; 2 for invalid arguments; 3 for a diagnostic-file write failure; 4 for a non-manual, busy, unavailable, or rejected operation; and 5 for an adopted mode whose setting could not be saved. The final switch result is reported through notices and logs.

Compatibility reports retain existing fields and record the manufacturer, model, graphics adapters, per-rate validation codes, configured targets, and resolved targets. `nominalHz` is the integer rate label, `desktopHz` is the display path's desktop rate, and `physicalHz` is the signal rate. `physicalModesKnown` and `physicalModesError` report physical-capability query status. `rateValidation[].origin` distinguishes `physical`, `virtual`, and `unverified`; `tested` indicates whether the `CDS_TEST` driver pre-check was performed. Filtered rates include a reason. Actual hardware switching results are recorded separately. **Serial numbers are not collected, and reports are not automatically uploaded.** On another laptop, generate a report first and perform the checks in the [compatibility notes](docs/laptop-compatibility.md). Successful driver validation alone does not mean that the laptop has completed hardware acceptance testing.

## Development and validation

LaptoHz uses C++20, CMake, and the Win32 API. In an MSVC environment, run `scripts/build.ps1`. For the portable LLVM-MinGW environment, run `scripts/bootstrap.ps1`, then `scripts/build.ps1 -Portable`. Tools live in the ignored `.tools` directory without changing the system PATH. The output is `dist/LaptoHz/LaptoHz.exe`, accompanied by Chinese and English documentation, compatibility notes, the MIT license, toolchain license notices, and SHA256 checksums.

```powershell
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Portable
.\dist\LaptoHz\LaptoHz.exe --version
```

Default CTest coverage includes policy, confirmation, physical/virtual-mode filtering, backend recovery, settings, log paths, startup registration, popups, dynamic native menus, mode hints, the full controller, and read-only display validation. Recovery tests inject state reads, display calls, and waits into the actual backend, covering bounded readback, failure, cancellation, environment changes, fractional frequencies, and surrounding parameters that were not restored. Production uses the Windows interfaces. Popup tests use real backend error text and cover unsaved settings, simulated 96/120/144/168/192 DPI messages, work-area bounds, focus, confirmation requests, and the nine-second timer. Menu tests cover real mouse hover and whole-menu repainting; hint tests cover delay, all three explanations, leaving/closing, keyboard input, focus, work-area bounds, and themes. Controller tests use isolated configuration and simulated hardware without changing the actual refresh rate or startup registration. Relevant tests skip when no internal display or interactive desktop is available. UI captures are stored in `build/portable-release/menu-captures`, `popup-captures`, `hint-captures`, and `controller-captures`.

```powershell
# Explicit hardware switching: test each valid rate, then restore the original actual rate
# First select Manual mode in the running application to avoid automatic-rule interference
.\dist\LaptoHz\LaptoHz.exe --mode manual
.\build\portable-release\display_smoke_test.exe --switch
```

`scripts/validate-runtime.ps1` checks startup toggling, launching through the shortcut, and idle overhead. It changes the startup preference and does not sign out or restart Windows. The user reported that actual power-source changes, sleep/lock recovery, and sign-in startup passed on the tested laptop. Multiple displays, actual mixed-DPI use, HDR/games, and other laptops still require separate manual acceptance records.

Tests also cover translation completeness, language fallback, settings migration, and save failures. English native popups cover long recovery errors, all three themes, and simulated 100%/150%/200% DPI messages. Controller tests exercise language selection through the actual menu, immediate translation of an existing failed confirmation, and language changes during a manual display operation.

## Implementation references

- [Compatible display-mode enumeration](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsexw)
- [Graphics-kernel mode lists](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtgetdisplaymodelist) and [physical/virtual mode flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmdt_displaymode_flags)
- [Display-path queries](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig)
- [Display-mode validation and switching](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexw)
- [Tray positioning](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyicongetrect) and [native menus](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenuex)
- [Native-menu selection notifications](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-menuselect) and [Windows tracking tooltips](https://learn.microsoft.com/en-us/windows/win32/controls/implement-tracking-tooltips)
- [Windows themes](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes) and [owner-drawn menus](https://learn.microsoft.com/en-us/windows/win32/menurc/using-menus#creating-owner-drawn-menu-items)
- [Actual file-path resolution](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew) and [Shell shortcuts](https://learn.microsoft.com/en-us/windows/win32/shell/links)
- [Windows display language](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getuserdefaultuilanguage) and [system-error messages](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessagew)
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw), [CMake](https://cmake.org/), and [Ninja](https://ninja-build.org/)

## License

LaptoHz is licensed under the [MIT License](LICENSE). Third-party toolchains and components retain their own license notices; see the [LLVM-MinGW license](licenses/LLVM-MinGW-LICENSE.txt).
