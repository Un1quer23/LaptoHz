# LaptoHz

**语言：** 简体中文 | [English](README.en.md)

笔记本内屏刷新率切换工具，支持按供电自动切换、切换前确认和手动选档。

Windows 11 x64 内屏刷新率切换工具，当前版本 **0.3.0-beta.5**。按 Windows 报告的内屏物理能力检测档位，可用于不同品牌笔记本；这是通用测试版，已实测机型和待验收范围见 [适配记录](docs/laptop-compatibility.md) 与 [验证记录](VALIDATION.md)。本版补充切换失败后的恢复结果校验，明确区分恢复已验证、请求失败、回读未通过、操作取消和环境变化；沿用三个模式、确认流程、9 秒结果提示、物理档位筛选及当前托盘图标。

| 模式 | 行为 |
|---|---|
| 自动 | 按供电规则切换，启动、唤醒和显示能力变化后核对 |
| 确认 | 实际供电状态变化后询问，确认才切换 |
| 手动 | 主动选择刷新率，电源变化和唤醒不覆盖 |

默认插电使用**最高可用档位**；电池优先 **60Hz**，其次 59Hz。两者都不存在时，选择最低的 ≥60Hz 档位；全部低于 60Hz 时选择最高档。检测到且通过校验的其他物理档位，也可手动选择或设为目标。

## 使用

把 EXE 放在固定目录后双击运行，无需管理员权限或额外 C++ 运行库。首次运行默认开启当前用户登录自启。图标可能在任务栏隐藏图标区域。

当前程序界面为简体中文；英文 README 中的菜单名称用于说明。

**左右键打开同一个托盘菜单**，顶部显示当前刷新率、供电状态和模式。桌面刷新率与实际信号不同时分别显示，例如 `30Hz（信号 60Hz）`。方向键、Enter、Esc 可操作；点击外部关闭。菜单、确认窗和结果提示跟随 Windows 应用深浅色，高对比度使用系统配色。字母、数字快捷选择已移除。

第一次使用可以按以下步骤操作：

1. 左键或右键点托盘图标，查看顶部的当前刷新率、供电状态和模式。
2. 在“自动模式”“确认模式”“手动模式”上停留约 **0.5 秒**，菜单旁会显示对应操作说明；方向键选中模式也可查看。移动到其他选项、离开菜单或关闭菜单时提示消失。
3. 要按电源状态切换，选择自动模式；要每次插拔先决定，选择确认模式。两个模式都使用菜单中设置的插电／电池目标。
4. 要立即手动切换，先选手动模式，再点下方可用的刷新率。标“当前”的灰色档位表示已经处于该档，无需再次选择。

| 模式提示 | 内容 |
|---|---|
| 自动模式 | 按插电／电池目标自动切换；进入此模式，以及启动、唤醒时核对刷新率 |
| 确认模式 | 插拔电源后先询问，确认才切换；提示保持显示，选择“保持当前”或 × 可放弃本次切换 |
| 手动模式 | 先选此模式，再点击下方刷新率；插拔电源和唤醒不会覆盖手动选择 |

悬停和方向键高亮只显示说明；点击模式或按 Enter 才选择模式。小提示不会抢焦点或遮住菜单项，配色跟随菜单。

| 菜单项 | 行为 |
|---|---|
| 自动／确认／手动模式 | 互斥选择，跨程序重启保存 |
| 插电目标 | 子菜单选择“默认：最高可用”或当前有效档位 |
| 电池目标 | 子菜单选择“默认：优先 60Hz”或当前有效档位 |
| 手动刷新率 | 当前内屏支持且通过驱动校验的档位直接显示在主菜单中 |
| 登录 Windows 时自动运行 | 开关当前用户登录自启 |
| 查看诊断日志 | 用系统记事本打开本地运行和排障记录 |
| 退出 | 保留当前刷新率，结束程序 |

**先进入手动模式，再选择刷新率。** 进入手动模式保持当前显示状态、撤销旧电源任务和待确认建议，菜单在原位置继续显示。当前档位置灰并标“当前”；分数频率对应的等效档位也禁用，标“当前等效”。手动档位没有勾或单选圆点，模式组和两个目标子菜单各自使用单选圆点，自启使用勾。档位较多时由原生菜单滚动。

修改目标不会改变模式：自动模式立即核对；确认模式撤销旧建议并以当前供电为新基线，下次实际供电变化再询问；手动模式保持实际刷新率。切换处理中禁用目标修改和重复切档。选中的自定义目标暂不可用时保留设置、显示原因，暂停对应供电规则；另一供电规则仍可使用。默认策略随能力变化重新解析，不把当前机器的最高档写成固定默认值。

自动模式下，通过 Windows 设置等工具改动刷新率后，会保持该选择，直到下一次实际供电变化、启动、唤醒、内屏恢复、输出路径或显示能力变化。只有当前 Hz 变化不算能力变化。

确认模式在启动、进入模式或修改目标时建立供电基线；同一电源下的显示通知和唤醒不新建建议。睡眠期间换电源，恢复后补问；已符合推荐档位时不新建窗口。已有建议会随再次插拔或显示能力变化更新，旧请求不能执行。

确认窗无倒计时，不抢焦点，提供“切换到某档位”“保持当前”和 ×。保持或 × 只放弃本次建议，保留确认模式。失败时显示原因并允许重试；锁屏、息屏和睡眠期间暂时隐藏。成功后显示独立的结果提示，× 可手动关闭，右下角每秒显示 `9s → 8s → 7s → 6s → 5s → 4s → 3s → 2s → 1s`，9 秒后关闭。

## 支持边界

只处理一个可明确识别的内屏。使用当前分辨率、色深、方向等参数相同的兼容模式，核对 Windows 图形内核的物理／虚拟模式标记，经 `CDS_TEST` 校验后才提供物理档位；不启用原始模式枚举，不创建超频或自定义显示模式。Windows 的整数标称 Hz 用于选档，实际信号 Hz 单独回读，兼容 59.94、119.88 等分数频率和驱动取整报告。能力接口读取失败时暂停切换并记录原因，不退回未经确认的虚拟列表。

可选档位由当前内屏、分辨率及驱动能力决定。虚拟桌面档位不会作为切换目标；外部工具选择此类档位后，状态仍分别显示桌面和信号。旧配置中的虚拟目标会保留并标为暂不可用。切换结果必须同时满足桌面档位和物理信号校验。DRR 开启时仍不切换。[微软模式标记说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmdt_displaymode_flags)

切换只改变刷新率；回读验证分辨率、色深、方向、排列、缩放和高级颜色状态。目标回读不符时，仅在操作未取消、原内屏输出路径与显示能力仍匹配的前提下，请求恢复原模式一次。恢复接口成功后立即回读，最多 4 次、间隔 150ms；分别核对操作前的标称、桌面和物理信号频率，以及上述周围参数。实际频率采用 0.1Hz 容差，整数取整别名须有分数信号证据；原状态的桌面／信号差异分别保留，未知或无效频率不能判为恢复成功。取消或环境变化时停止校验并记录原因。驱动可能使屏幕短暂黑屏。

恢复请求成功与恢复已验证分别记录。提示、确认窗和日志说明最终恢复结果；即使恢复已验证，目标切换仍报告失败，确认建议继续保留供用户重试，不自动再次切向目标。无法验证时记录最后观察到的显示状态；恢复接口返回码与原目标接口返回码分别保存。

外接屏扩展模式允许使用，外接屏参数保持；仅外接屏输出时等待内屏恢复。**复制模式、多个内屏、远程／非活动会话和 Windows 动态刷新率（DRR）继续不执行切换**。工具不自动关闭 DRR，需在 Windows 高级显示设置中关闭。只有一个可用档位时显示说明，不重复切换。首版不包含 ARM64、Windows 10 专项验收、外接屏控制或厂商专用接口。

## 配置与日志

通常位于 `%LOCALAPPDATA%\RefreshRateSwitcher`；改名为 LaptoHz 后沿用此目录，以保留已有设置和日志。启动环境重定向 AppData 时，实际文件位于该环境的应用数据目录。

```ini
[App]
StartupInitialized=1
Mode=auto

[RefreshRate]
AC=auto
Battery=auto
```

`Mode` 可为 `auto`、`confirm`、`manual`。`AC` 和 `Battery` 为 `auto` 或整数 Hz。旧配置缺少目标字段时采用动态默认值，保留模式和自启选择；目标格式错误时仅对应项回退默认并记录日志。不可用的合法整数目标保留，不偷偷替换。

保存失败时，本次运行采用用户选择，状态标“设置未保存”，并提示失败；重启后只能读取实际保存成功的设置。手动模式不在启动时重应用上次档位。

`switcher.log` 记录启动来源、模式／目标变化、切换请求、目标接口返回码及独立的恢复状态和返回码；轮转文件为 `switcher.previous.log`，每份约 256KiB。菜单打开日志前取得文件句柄的实际路径，再交给系统记事本，保留 0.2.8 的 AppData 路径修复。日志保存在本机，不自动上传。

自启使用当前用户启动文件夹的 `LaptoHz.lnk`，参数 `--startup`，工作目录为 EXE 所在目录；在资源管理器输入 `shell:startup` 可查看。保留旧 Run 项迁移、Windows 禁用选择和关闭自启后不重开的逻辑。移动或删除程序前，先关闭自启并退出。启动文件夹启动测试不等于真正重登验收。

从旧名 Refresh Rate Switcher 版本升级时，先在旧版菜单关闭登录自启并退出，再运行 `LaptoHz.exe`；如需登录自启，在新版菜单重新开启。已有模式和刷新率目标继续读取原配置。旧版与新版共享单实例标记，避免同时运行两份程序。

## 控制与适配报告

```powershell
# 只读诊断，不启动托盘或注册自启；输出 JSON 适配信息
.\LaptoHz.exe --diagnose --output .\diagnostics.json

# 控制已运行实例
.\LaptoHz.exe --mode manual
.\LaptoHz.exe --switch 60
.\LaptoHz.exe --mode confirm
.\LaptoHz.exe --mode auto
```

`--switch <整数Hz>` 要求已经进入手动模式，且目标档位当前可用。当前档位只刷新状态，不重复应用或弹多余提示。`--pause` 进入手动，`--resume` 进入自动；`--status` 显示状态，`--exit` 退出，`--version` 输出版本。

控制应答超时为三秒。返回码：0 已采用模式／接受切换请求；1 未运行、未响应或旧版本不支持；2 参数格式错误；3 诊断文件写入失败；4 非手动、忙碌、目标／会话不可用或控制被拒绝；5 模式已采用但保存失败。最终切换结果仍通过提示和日志反馈。

适配报告保留旧字段，记录厂家、机型、显卡、逐档校验代码、配置目标及解析结果；`nominalHz` 是整数标称档位，`desktopHz` 是显示路径的桌面频率，`physicalHz` 是实际信号频率，三者分别记录。`physicalModesKnown`／`physicalModesError` 记录物理能力读取结果；`rateValidation[].origin` 区分 `physical`、`virtual`、`unverified`，`tested` 表示是否执行了 `CDS_TEST` 驱动预检查，被过滤档位会说明原因；实机切换结果另记验证记录。**不记录序列号，不自动上传**。在其他笔记本上先生成报告，再按 [适配记录](docs/laptop-compatibility.md) 的步骤人工验收。驱动校验成功不代表已完成该机型的实测。

## 开发与验证

使用 C++20、CMake、Win32 API。MSVC 环境运行 `scripts/build.ps1`；便携 LLVM-MinGW 环境先运行 `scripts/bootstrap.ps1`，再运行 `scripts/build.ps1 -Portable`。工具链位于被忽略的 `.tools`，不改系统 PATH；输出为 `dist/LaptoHz/LaptoHz.exe`，同目录包含中英文说明、适配记录、MIT 许可证、工具链许可和 SHA256。

```powershell
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Portable
.\dist\LaptoHz\LaptoHz.exe --version
```

软件图标采用“笔记本 + Hz + 双向切换箭头”，包含 16–256 像素尺寸。托盘单独绘制 16–64 像素的笔记本和切换箭头，增大可见区域，并根据任务栏主题选择深浅线条、按任务栏 DPI 载入。使用 `node scripts/create-icon.cjs` 从源图重新生成，详见[图标设计与提示词](docs/icon-design.md)。

默认 CTest 覆盖策略、确认、物理／虚拟档位筛选、显示后端恢复流程、配置、日志路径、自启、弹窗、动态原生菜单、模式操作提示、完整控制器和只读显示校验。恢复测试向实际后端注入状态读取、显示调用与等待函数，覆盖有限回读、失败、取消、环境变化、分数频率和周围参数未恢复；默认运行仍使用 Windows 接口。菜单测试包含真实鼠标悬停与整框重绘检查；模式提示测试覆盖显示延迟、三种说明、移开／关闭、键盘、焦点、工作区与主题。控制器使用隔离配置和模拟硬件，不改真实刷新率或自启。无内屏／非交互桌面相应测试跳过；界面截图在 `build/portable-release/menu-captures`、`popup-captures`、`hint-captures` 和 `controller-captures`。

```powershell
# 明确执行真实硬件切换：轮流测试所有有效档位，恢复原实际档位
# 先将正在运行的工具设为手动，避免自动规则干扰
.\dist\LaptoHz\LaptoHz.exe --mode manual
.\build\portable-release\display_smoke_test.exe --switch
```

`scripts/validate-runtime.ps1` 检查自启开关、快捷方式启动与空闲开销，会改变自启选择，不注销或重启 Windows。真实插拔、睡眠、重登、多屏、混合 DPI、HDR／游戏及其他笔记本仍须分别记录人工验收。

## 实现依据

- [兼容显示模式枚举](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsexw)
- [图形内核模式列表](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtgetdisplaymodelist)、[物理／虚拟模式标记](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmdt_displaymode_flags)
- [显示路径查询](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig)
- [显示模式校验与切换](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexw)
- [托盘位置](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyicongetrect)、[原生菜单](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenuex)
- [原生菜单选中通知](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-menuselect)、[Windows 跟踪提示](https://learn.microsoft.com/en-us/windows/win32/controls/implement-tracking-tooltips)
- [Windows 主题](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes)、[菜单自绘](https://learn.microsoft.com/en-us/windows/win32/menurc/using-menus#creating-owner-drawn-menu-items)
- [文件实际路径查询](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew)、[Shell 快捷方式](https://learn.microsoft.com/en-us/windows/win32/shell/links)
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw)、[CMake](https://cmake.org/)、[Ninja](https://ninja-build.org/)

## 许可证

LaptoHz 使用 [MIT 许可证](LICENSE)。第三方工具链及组件保留各自的许可声明，参见 [LLVM-MinGW 许可](licenses/LLVM-MinGW-LICENSE.txt)。
