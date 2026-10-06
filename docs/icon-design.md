# 笔记本刷新率切换图标

使用宽屏笔记本轮廓、轻字重的 **Hz** 和一条开放式双向箭头，表达内屏刷新率切换。采用石墨灰与米白；设备外部为真实透明背景。

- 源图：`resources/app-icon.png`，由内置 `image_gen` 工具生成并统一字重。
- 软件资源：`resources/app.ico`，包含 16、20、24、32、40、48、64、96、128、256 像素。16–48 像素以几何图形绘制；64 像素及以上使用源图。
- 托盘独立使用 `resources/tray-light.ico` 和 `resources/tray-dark.ico`，各包含 16、20、24、32、40、48、64 像素。图形只保留笔记本和大号双向箭头，统一线宽、增高屏幕并缩小透明边距；底座为连续实心图形，去掉中间的小方块；桌面图标继续保留 Hz。
- 深色任务栏使用米白线条，浅色任务栏使用石墨灰线条。颜色根据 Windows 的 `SystemUsesLightTheme` 选择，高对比度主题根据系统背景色选择；主题变化时更新。
- 托盘通过 `GetDpiForWindow` 读取任务栏 DPI，再用 [`GetSystemMetricsForDpi`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsystemmetricsfordpi) 和 `LoadImageW` 载入对应尺寸；显示器或 DPI 变化时重新读取。

重新生成 ICO：

```powershell
node scripts/create-icon.cjs
```

脚本仅使用 Node.js 标准库，读取非交错的 8 位 RGBA 源图，按预乘透明度缩小大尺寸，写入小尺寸位图与大尺寸 PNG。重新生成无需调用图片服务。

2026-10-04 托盘尺寸修订：本机任务栏 DPI 为 144（150%），图标格子为 24×24 像素。旧版浅色可见区域仅约 16×10，新版为 24×22；已检查原尺寸及像素放大对比。

验证：Release 编译成功，全部 10 项 CTest 通过（57.39 秒）。Windows 原生接口成功读取深浅托盘资源的全部 14 个尺寸，EXE 中资源 201／202 与 ICO 同尺寸像素一致，四角透明。此前资源 101 的全部 10 个尺寸也已验证。

## 图片提示词

使用内置图片工具；先生成新的轮廓与排版，再统一字重。完整提示词如下。

### 生成轮廓与排版

```text
Use case: logo-brand
Asset type: a refined minimalist application icon for a laptop refresh-rate switching utility.
Primary request: redraw the icon with elegant laptop proportions and a coherent typographic / stroke system. The previous icon was too bulky, with a heavy trapezoid base, oversized block lettering, and mismatched arrow and border weights.
Subject: a slim open laptop viewed directly from the front. Its screen is a LANDSCAPE rectangle, about 1.55 times wider than tall, with modest softly rounded corners. Draw the laptop as a clean charcoal outline filled with ivory. The bezel is a SINGLE refined medium-weight line, not a thick solid frame. Directly below it is a shallow laptop base only about one eighth of the screen height, drawn with the SAME stroke thickness and soft joins. The base subtly widens to each side and has one tiny centered hinge/trackpad notch. No heavy pedestal and no keyboard detail.
Screen content: exactly "Hz", capital H and lowercase z, set in a restrained modern geometric sans-serif MEDIUM weight. H and z share a correct baseline, clean proportions, balanced spacing, and even stem thickness. Avoid chunky block letters or custom sci-fi typography. The lettering is comfortably sized, occupying about one third of the screen width, with ample breathing room.
Below "Hz" place ONE compact horizontal double-ended switching arrow: a straight shaft with OPEN chevron arrowheads pointing left and right. No filled triangular arrowheads. The arrow strokes, letter stems, screen border, and base outline must have the SAME OPTICAL WEIGHT. Rounded line caps and joins. Do not add a second large arrow above the text.
Style: rigorously flat vector graphic, precise geometry, calm visual balance, modern desktop-app icon craftsmanship, strong small-size legibility. Use only solid neutral charcoal #34363B and ivory #F5F3EE. True alpha transparency outside the laptop silhouette. No extra outer halo or doubled border.
Composition: one centered laptop icon on a square transparent canvas, about 85 percent canvas width. Its overall silhouette is wide and shallow, rather than a chunky square. Keep all elements optically aligned.
Text (verbatim): "Hz". No other text.
Avoid: gloss, highlights, gradients, texture, drop shadows, 3D, realistic device rendering, detailed keyboard, large filled black base, thick black bezel, clumsy block font, clock, blue, mockup, watermark, or sheet of alternatives. Exactly one icon.
```

### 统一字重与画布

以第一步图形为编辑目标：

```text
Use case: precise-object-edit
Asset type: production app icon.
Input image: edit target, the supplied slim laptop refresh-rate icon.
Change only typographic / stroke harmony and the square export canvas. Preserve the existing elegant wide laptop silhouette, its shallow base, neutral charcoal and ivory palette, simple single border, and screen-content arrangement.
Make the exact "Hz" LETTERING LIGHTER: reduce the current H and z stem thickness by about 35 percent, with clean modern sans-serif proportions and correct shared baseline. Keep approximately the same letter height and spacing. It should look like a refined MEDIUM / REGULAR sans-serif, not bold block lettering. The new letter stems should visually match the width of the laptop's screen outline.
Make the single horizontal double-ended arrow shaft and its open chevron heads exactly the same weight as those new letter stems and the screen outline. Keep the arrow compact and centered below Hz with generous spacing.
All primary strokes must have one consistent optical weight. Keep corners and joins softly rounded, precise, and restrained.
The only text is "Hz", capital H and lowercase z, verbatim.
Export exactly one icon on a SQUARE (1:1 aspect) transparent canvas. Center the complete laptop with comfortable transparent padding, without stretching or changing its wide device proportions. True alpha transparency outside the laptop.
Use completely flat solid fills. No highlights, gradients, gloss, realistic shading, texture, shadow, thick bezel, filled arrowheads, additional rim, extra text, mockup or watermark.
```
