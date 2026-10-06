# 相近名称刷新率工具对比

研究日期：2026-10-05。对照本项目当前工作区的 **0.3.0-beta.2**。

结论：按供电切换高低刷新率已经有直接竞品。本项目最值得突出的是**明确管理笔记本内屏、区分物理与虚拟档位、切换后核对结果，以及自动／确认／手动三种可持续使用的模式**。这些结论限定于此次审阅的项目，不代表全市场独有。

本次只读审阅六个开源项目的默认分支源码和一个项目的官方说明，没有安装或运行竞品，也没有重新执行本项目的硬件测试。开源来源固定在下表提交，避免 README 更新或未来实现变化影响结论。官方说明没有交代的功能标为“未确认”，不视为不存在。网页刷新、下拉刷新和其他行业的同名工具不属于本次功能竞品。

## 比较概览

| 工具 | 已确认的功能和适用方向 | 与本项目的主要差异 |
|---|---|---|
| [PowerRefreshSwitcher](https://github.com/Defflorame/PowerRefreshSwitcher/blob/73e197bf55fc72393f331b189cbc3a8deb3a5e0f/PowerRefreshSwitcher.cpp) | Windows；启动、供电变化和唤醒时自动应用；电池 60Hz、插电 144Hz；同时调整 Windows 电源模式和节能设置 | 是最接近的直接竞品之一。目标写在源码常量里，默认处理主屏；本项目提供菜单配置、内屏识别和三种模式，并增加物理能力筛选和切换后验证。它的电源设置联动范围更广 |
| [dynamic-refresh / Auto Refresh Rate Switcher](https://github.com/protocol4/dynamic-refresh/tree/18118fa3bd781fdccc23e0f3a87a7c892b6cbc52) | Windows；计划任务触发 PowerShell／VBScript 调用 QRes；插电 120Hz、电池 60Hz，编辑脚本改目标 | 同样覆盖核心供电自动切换。本项目将目标、当前状态、模式和错误原因放入托盘菜单，并自行检测内屏与档位；该脚本没有实现同样的物理信号结果核对流程，底层 QRes 未审计 |
| [rr-switcher（saddexed）](https://github.com/saddexed/rr-switcher/blob/a69b4e162c31437ae5ea5548d53a4be684af0b60/src/main.cpp) | Windows 命令行；指定 Hz、按编号选择屏幕、列出模式、可选永久保存 | 本项目增加供电策略、模式和图形界面；它能直接控制指定外接屏，适合脚本调用，本项目当前专注内屏 |
| [Refresh Rate Switcher（trevortmorgan）](https://github.com/trevortmorgan/refresh-rate-switcher/tree/951ae69e2c429f66cf31ab7807bc070f12e6062f) | Windows 托盘；逐屏手动选档、通知、登录自启；使用 .NET 8 | 本项目增加供电自动／确认策略、物理能力与信号核对，并以原生 C++ 程序交付。它的逐屏手动管理覆盖面更广；本项目现使用 LaptoHz 名称 |
| [HzSwitch（DExUS）](https://www.dexus5.com/system3/downloads/) | 官方说明：随启动的应用切换刷新率，也提供托盘手动切换 | 它的明确特色是按应用触发；本项目按供电管理。其电池规则、内屏识别、校验与恢复机制未确认 |
| [HzSwitch（AVAM16）](https://github.com/AVAM16/HzSwitch/blob/d3abb20f49d2d4393de9b6f05af9b53e5a55a300/contents/ui/main.qml) | Linux KDE Plasma 6 小组件；读取主屏模式，在当前分辨率的最低／最高档之间手动切换 | 本项目面向 Windows 笔记本，具有供电规则和三种模式。它的优势是直接融入 KDE 面板；已阅 QML 没有电源自动切换流程 |
| [gpu-rr-switcherd](https://github.com/sohamdhapre/gpu-rr-switcherd/tree/ea3be09f341b853b0c94eef56fa12b117a0c5c3b) | Linux GNOME／Wayland 守护程序；供电变化切换最低／最高档；开机时按供电禁用 NVIDIA 独显，运行中提示重启切换 GPU 状态 | 刷新率策略与本项目有重叠；它还覆盖独显管理。本项目的差异集中在 Windows 内屏、用户选择和显示结果验证，两者安装与系统作用范围明显不同 |

## 本项目已有的差异化

### 1. 按真实内屏能力选档，并核对实际信号

本项目把兼容当前显示参数的 GDI 档位与图形内核的物理能力列表取交集，排除虚拟刷新率，然后执行驱动预检查。切换后再次读取标称、桌面和物理信号频率，并检查显示参数；验证失败且原路径、能力仍有效时请求恢复原模式。恢复是有条件的请求，不能承诺驱动一定成功恢复。

这解决了一个实际遇到的问题：现有 ThinkBook 记录显示，旧版选择 30／48Hz 时实际信号仍为 60Hz，选择 120Hz 时信号为 240Hz；这些档位即使通过 GDI 预检查，也被当前驱动标为虚拟。本版提供该机物理 60／240Hz，其他机型确有物理低档位时仍按能力提供。

在此次审阅的 Windows 开源竞品中，PowerRefreshSwitcher 与 trevortmorgan 的项目也执行 `CDS_TEST`，不能说它们“没有校验”。差异在于本项目还做物理／虚拟分类和切换后实际信号、周围显示参数核对。trevortmorgan 切换后重建菜单时会读取当前整数 Hz，但没有以同样的完整结果验证流程判定成功并请求恢复。

依据：[本项目显示实现](../src/display.cpp)、[物理档位实现](../src/physical_modes.cpp)、[实机记录](../VALIDATION.md)、[微软模式标记定义](https://github.com/microsoft/libdxg/blob/main/include/dxg/d3dkmthk.h)；竞品的 [PowerRefreshSwitcher 切换流程](https://github.com/Defflorame/PowerRefreshSwitcher/blob/73e197bf55fc72393f331b189cbc3a8deb3a5e0f/PowerRefreshSwitcher.cpp#L152-L208) 与 [trevortmorgan 切换实现](https://github.com/trevortmorgan/refresh-rate-switcher/blob/951ae69e2c429f66cf31ab7807bc070f12e6062f/src/RefreshRateSwitcher/Core/DisplayManager.cs)。

### 2. 内屏识别体现笔记本定位

本项目按 INTERNAL／LVDS／嵌入式 DisplayPort 等连接类型识别一个明确的内屏，扩展外接屏的参数纳入保持检查。外接屏成为主屏时，自动规则的目标仍然是内屏；内屏关闭时等待恢复。复制模式、多个内屏、远程／非活动会话、DRR 等状态有明确的暂停或拒绝边界。

PowerRefreshSwitcher 默认按“主显示器”选择目标，修改常量可以处理所有连接到桌面的显示器；KDE HzSwitch 选优先级为 1 的屏幕；gpu-rr-switcherd 使用第一项显示器和逻辑显示器。它们的这些选择逻辑与“识别并只管理内屏”不同。rr-switcher 和 trevortmorgan 则允许用户选屏，属于更广的显示器控制用途。

依据：[本项目内屏识别](../src/display.cpp)、[策略边界](../src/policy.cpp)、[PowerRefreshSwitcher 选屏逻辑](https://github.com/Defflorame/PowerRefreshSwitcher/blob/73e197bf55fc72393f331b189cbc3a8deb3a5e0f/PowerRefreshSwitcher.cpp#L102-L108)、[KDE 选屏与模式逻辑](https://github.com/AVAM16/HzSwitch/blob/d3abb20f49d2d4393de9b6f05af9b53e5a55a300/contents/ui/main.qml#L39-L82)、[GNOME 显示实现](https://github.com/sohamdhapre/gpu-rr-switcherd/blob/ea3be09f341b853b0c94eef56fa12b117a0c5c3b/src/displayManager.cpp)。

### 3. 自动、确认、手动构成完整的用户选择

三个模式互斥并跨应用重启保存。自动模式随规则核对；确认模式在实际供电变化后询问，保持或关闭只放弃当次建议；手动模式保留用户选择，电源变化和唤醒不覆盖。进入手动时还撤销旧电源任务和待确认建议，过期确认请求不能在屏幕或目标变化后继续执行。

这比单纯添加一个“自动开关”更适合不同使用习惯，例如希望插拔时询问的用户，或者临时保持高刷的电池用户。此次审阅的六个开源项目没有呈现同样的三模式组合；DExUS 未审阅源码，不作缺失判断。

依据：[本项目使用说明](../README.md)、[确认与自动策略](../src/policy.cpp)、[控制器](../src/app.cpp)。

### 4. 按机型动态解析目标，菜单即可修改

默认插电用最高有效物理档位；电池优先 60／59Hz，再按支持档位回退。两个目标均可在菜单里修改，不需要用户编辑源码或脚本。自定义目标暂不可用时保留原选择并显示原因，仅暂停对应供电规则，避免静默换成另一档位。

PowerRefreshSwitcher 的 60／144Hz 写在 C++ 常量里；dynamic-refresh 的 60／120Hz 写在脚本里。gpu-rr-switcherd 已经动态取最低／最高档，所以“动态最高档”本身也不是本项目独有，差异在目标可配置与模式语义的组合。

依据：[本项目默认策略](../src/policy.cpp)、[使用与配置](../README.md)、[PowerRefreshSwitcher 常量](https://github.com/Defflorame/PowerRefreshSwitcher/blob/73e197bf55fc72393f331b189cbc3a8deb3a5e0f/PowerRefreshSwitcher.cpp#L13-L15)、[dynamic-refresh 脚本](https://github.com/protocol4/dynamic-refresh/blob/18118fa3bd781fdccc23e0f3a87a7c892b6cbc52/qres.ps1)。

### 5. 支持适配排障，信息能够解释“为什么不能切”

本项目的只读 JSON 报告区分标称／桌面／信号 Hz、物理／虚拟／未确认档位、预检查结果及配置目标解析结果；本地轮转日志记录操作与错误。这对收集不同笔记本和驱动的适配证据有用。其他项目也有控制台输出或日志，差异是与内屏物理能力、策略状态和结果核对相连的结构化诊断内容。

中文托盘界面、主题跟随、普通用户运行和无额外 C++ 运行库的交付降低日常使用门槛。这些是体验价值，不能据此推断占用或性能一定胜过所有竞品。

依据：[诊断输出](../src/display.cpp)、[使用说明](../README.md)、[适配记录](laptop-compatibility.md)。

## 竞品更强或覆盖更广的方向

- **电源设置联动：** PowerRefreshSwitcher 同时设置电源模式／计划与 Energy Saver。本项目当前的供电策略作用于刷新率。若突出“Power”，需让介绍准确表达这一范围。依据：[电源设置实现](https://github.com/Defflorame/PowerRefreshSwitcher/blob/73e197bf55fc72393f331b189cbc3a8deb3a5e0f/PowerRefreshSwitcher.cpp#L215-L340)。
- **逐屏控制：** rr-switcher 可按编号操作屏幕；trevortmorgan 的托盘菜单可逐屏选档。本项目内屏专注度更高，但外接屏管理范围更窄。依据：[CLI 参数与选屏](https://github.com/saddexed/rr-switcher/blob/a69b4e162c31437ae5ea5548d53a4be684af0b60/src/main.cpp#L123-L189)、[逐屏托盘菜单](https://github.com/trevortmorgan/refresh-rate-switcher/blob/951ae69e2c429f66cf31ab7807bc070f12e6062f/src/RefreshRateSwitcher/UI/TrayApplication.cs#L79-L121)。
- **按应用触发：** DExUS HzSwitch 官方明确介绍随启动应用切换。本项目尚无游戏或应用规则。依据：[官方说明](https://www.dexus5.com/system3/downloads/)。
- **Linux 和独显管理：** KDE HzSwitch 提供 Plasma 集成；gpu-rr-switcherd 结合 GNOME 供电刷新率与开机 NVIDIA 驱动／PCI 设备处理。后者需要系统级安装，GPU 状态切换提示重启。依据：[KDE 元数据](https://github.com/AVAM16/HzSwitch/blob/d3abb20f49d2d4393de9b6f05af9b53e5a55a300/metadata.json)、[Linux 项目说明](https://github.com/sohamdhapre/gpu-rr-switcherd/blob/ea3be09f341b853b0c94eef56fa12b117a0c5c3b/README.md)、[安装指南](https://github.com/sohamdhapre/gpu-rr-switcherd/blob/ea3be09f341b853b0c94eef56fa12b117a0c5c3b/SetupGuide.md)。

## 审阅边界和产品表述

供电自动切换、登录自启、唤醒后应用、托盘、手动选档和事件驱动实现，在这些项目中都有相应先例。它们可以作为功能说明，不宜单独作为“独有特色”。

trevortmorgan 仓库虽然存在 ProfileManager、HotkeyManager、DisplayChangeMonitor 等类，但已阅入口和托盘 UI 没有接通配置档选择、热键或自动显示监控流程；README 也将配置档和热键列在路线图。此次未把“有类文件”认定为可用产品功能。其切换实现包含驱动预检查；没有找到本项目同样的物理模式过滤和失败恢复流程。依据：[入口](https://github.com/trevortmorgan/refresh-rate-switcher/blob/951ae69e2c429f66cf31ab7807bc070f12e6062f/src/RefreshRateSwitcher/Program.cs)、[托盘 UI](https://github.com/trevortmorgan/refresh-rate-switcher/blob/951ae69e2c429f66cf31ab7807bc070f12e6062f/src/RefreshRateSwitcher/UI/TrayApplication.cs)、[路线图](https://github.com/trevortmorgan/refresh-rate-switcher/blob/951ae69e2c429f66cf31ab7807bc070f12e6062f/README.md#L212-L220)。

dynamic-refresh 的脚本读取 `root\wmi` 的 `BatteryStatus.PowerOnline`，与 README 示例不同。脚本仅传 `/r` 给 QRes，没有明确识别内屏、检查实际信号或恢复的逻辑；QRes 可执行文件内部的选屏与检查没有审阅，不能据此断言 QRes 完全不做校验。计划任务订阅电源和 Winlogon 日志；没有单独的睡眠恢复策略，但日志也可能间接触发，未实测。依据：[脚本](https://github.com/protocol4/dynamic-refresh/blob/18118fa3bd781fdccc23e0f3a87a7c892b6cbc52/qres.ps1)、[任务定义](https://github.com/protocol4/dynamic-refresh/blob/18118fa3bd781fdccc23e0f3a87a7c892b6cbc52/taskschd.xml)。

DExUS 官网直接打开超时，本次通过搜索服务取得官方页面内容。只能确认官方描述的应用触发和托盘手动切换，没有据此验证其当前 Windows 11 兼容性、内屏选择、电源规则或切换校验。

本项目现有实机证据来自 **ThinkBook 16p 2025** 的检测和 **240→60→240Hz** 切换记录；真实电源插拔、睡眠期间换电源、锁屏／息屏、重登、多屏／HDR 和其他机型仍待验收。模式与通知的模拟测试不能替代这些实机测试。没有相同环境下的竞品对照运行，也没有续航或功耗对照测量，因此当前可证明的是实现差异和已有单机记录，不能宣称普遍更稳定或更省电。依据：[验证记录](../VALIDATION.md)、[适配范围](laptop-compatibility.md)。

建议对外定位：**面向笔记本内屏的刷新率管理工具，随供电切换，提供自动、确认和手动选择，并核对切换结果。** 下一步最有价值的是扩大实机验收和记录切换失败原因，形成可追溯的兼容性资料；功耗或续航测量完成后，再加入量化省电表述。
