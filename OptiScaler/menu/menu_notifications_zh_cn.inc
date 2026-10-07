// Native notification supplement dictionary (Simplified Chinese).
//
// Second { "English", "Chinese" }, row list, included immediately after
// menu_zh_cn.inc inside OptiI18n::Detail::Entries() in menu/menu_i18n.h. It
// carries the visible native notification strings and the user-visible active
// game-quirk descriptions (stringQuirks in dllmain.cpp) that the generated
// main-menu catalog (menu_zh_cn.inc) does not cover; the main catalog and its
// assembly are untouched.
//
// English stays the authoritative source and the fallback. Rows below keep the
// exact newlines, `\n` escapes and `{}` / `%` placeholders of the English
// literal. Static toast titles/content are translated automatically by the
// existing ImGui render hooks, so they are listed verbatim here and are not
// wrapped at their call sites; only the dynamically formatted notifications
// (Streamline_Hooks / Nvngx_FG / XeSSFeature) translate the format literal
// before formatting. UTF-8 text is stored literally (not \x escapes) so the
// assemble step in work/prepare-ui-catalog.py can union its codepoints into the
// font-coverage output.
{ "That's likely unintended and will lead to big OptiScaler.log\nPlease disable logging to file and delete OptiScaler.log", "这很可能并非有意为之，且会导致 OptiScaler.log 变得很大\n请禁用文件日志记录并删除 OptiScaler.log" },
{ "Trace logging still active", "跟踪日志仍处于启用状态" },
{ "Can't init FSR FG\nAre you missing the required DLLs?", "无法初始化 FSR FG\n是否缺少所需的 DLL？" },
{ "Can't init XeFG\nAre you missing the required DLLs?", "无法初始化 XeFG\n是否缺少所需的 DLL？" },
{ "Can't init DLSSG Output\nAre you missing the streamline folder?", "无法初始化 DLSSG 输出\n是否缺少 streamline 文件夹？" },
{ "RTSS + XeFG detected", "检测到 RTSS + XeFG" },
{ "RTSS Reflex Injection is known to cause issues.\nEspecially when using XeFG.\nPlease disable it.", "已知 RTSS Reflex 注入会导致问题。\n尤其是在使用 XeFG 时。\n请将其禁用。" },
{ "Upscaler failed to run!", "超分辨率器运行失败！" },
{ "Error while compiling a shader!", "编译着色器时出错！" },
{ "Falling back to FSR 2.2", "回退到 FSR 2.2" },
{ "Falling back to FSR 2.1.2", "回退到 FSR 2.1.2" },
{ "Falling back to XeSS", "回退到 XeSS" },
{ "Failed to create a shared texture\nMake sure you are using at least Wine/Proton 11", "创建共享纹理失败\n请确保至少使用 Wine/Proton 11" },
{ "Couldn't load libxess.dll\nCheck if the dll is present", "无法加载 libxess.dll\n请检查该 dll 是否存在" },
{ "Couldn't load libxess_dx11.dll\nCheck if the dll is present", "无法加载 libxess_dx11.dll\n请检查该 dll 是否存在" },
{ "Couldn't create XeSS context\n{}", "无法创建 XeSS 上下文\n{}" },
{ "{} is not available.\nFalling back to {}.", "{} 不可用。\n正在回退到 {}。" },
{ "{} is not available and can't fallback", "{} 不可用且无法回退" },
{ "You are missing the following dlls from the streamline folder:\n", "streamline 文件夹中缺少以下 dll：\n" },
// Active game-quirk descriptions (stringQuirks in dllmain.cpp), user-visible in
// the native menu like any other label; every ASCII token, symbol and newline is
// kept exactly as in the English source.
{ "Fixing DLSSG's hudless state in Cyberpunk", "修复 Cyberpunk 中 DLSSG 的无 HUD 状态" },
{ "FSR FG hudless mismatch fixup", "FSR FG 无 HUD 不匹配修正" },
{ "Skipping first FSR 3 method", "跳过第一个 FSR 3 方法" },
{ "Load d3d12.dll", "加载 d3d12.dll" },
{ "Enable KernelBase hooks", "启用 KernelBase 钩子" },
{ "Fix DLSS/DLSSG barriers on Vulkan", "修复 Vulkan 上的 DLSS/DLSSG 屏障" },
{ "Force detected engine as Unreal Engine", "强制将检测到的引擎视为虚幻引擎" },
{ "Disabling Hudfix due to known issues", "因已知问题而禁用 Hudfix" },
{ "Enabling AutoExposure", "启用自动曝光" },
{ "Disable FFX Inputs", "禁用 FFX 输入" },
{ "Disable FSR 3.0 Inputs", "禁用 FSR 3.0 输入" },
{ "Disable FSR 2.X Inputs", "禁用 FSR 2.X 输入" },
{ "Disable Reactive Masks", "禁用反应性遮罩" },
{ "Enabling restore compute signature on AMD/Intel", "在 AMD/Intel 上启用恢复计算签名" },
{ "Enabling restore compute signature on Nvidia", "在 Nvidia 上启用恢复计算签名" },
{ "Extended signatures restore", "扩展签名恢复" },
{ "Dxgi spoofing disabled by default", "默认禁用 Dxgi 伪装" },
{ "Disable Use FSR Input Values", "禁用使用 FSR 输入值" },
{ "Disable custom pipeline creation for XeSS", "禁用 XeSS 的自定义管线创建" },
{ "Don't use NTShared enabled", "已启用不使用 NTShared" },
{ "Don't use color resource barrier fix for Unreal Engine games", "对虚幻引擎游戏不使用颜色资源屏障修复" },
{ "Don't use motion vector resource barrier fix for Unreal Engine games", "对虚幻引擎游戏不使用运动矢量资源屏障修复" },
{ "Skipping upscaling for first 10 frames", "跳过前 10 帧的超分" },
{ "Skip turning the first swapchain created into an FSR swapchain", "跳过将创建的第一个交换链转换为 FSR 交换链" },
{ "Correct simulation start marker's frame id", "修正模拟起始标记的帧 id" },
{ "Don't use V-Sync overrides", "不使用垂直同步覆盖" },
{ "Hack for broken Hitman reflex", "针对 Hitman Reflex 失效的修补" },
{ "Skipping D3D11 feature level elevation, native FSR3.1 will be disabled!", "跳过 D3D11 功能级别提升，原生 FSR3.1 将被禁用！" },
{ "Using kernel hooks instead of NTdll ones", "使用内核钩子代替 NTdll 钩子" },
{ "Use FSR2 pattern matching", "使用 FSR2 模式匹配" },
{ "Always capture FSR-FG swapchain", "始终捕获 FSR-FG 交换链" },
{ "Allowed Frame Ahead: 2", "允许提前帧数：2" },
{ "Skip pre init checks for XeFG", "跳过 XeFG 的预初始化检查" },
{ "Create D3D12 device for Luma before loading Reshade", "在加载 Reshade 之前为 Luma 创建 D3D12 设备" },
{ "Load vulkan-1.dll", "加载 vulkan-1.dll" },
{ "Use FSR2 DX11 inputs", "使用 FSR2 DX11 输入" },
{ "Use FSR2 Vulkan inputs", "使用 FSR2 Vulkan 输入" },
{ "Force Borderless when using XeFG", "使用 XeFG 时强制无边框窗口" },
{ "Override Vsync when using XeFG", "使用 XeFG 时覆盖垂直同步" },
{ "Force create D3D12 device for w/Dx12", "为搭配 Dx12 强制创建 D3D12 设备" },
{ "Disable Resize Skip", "禁用跳过尺寸调整" },
{ "Spoof Registry", "伪装注册表" },
{ "Disable fakenvapi", "禁用 fakenvapi" },
{ "Force depth as D32S8", "强制深度为 D32S8" },
{ "Don't Preserve FG Swapchain", "不保留 FG 交换链" },
{ "Using old overlay (draws on upscaled image)", "使用旧版叠加层（绘制在超分后的图像上）" },
{ "Fix DLSS quality selection in Pragmata", "修复 Pragmata 中的 DLSS 质量选择" },
{ "Ignore ValidUntilEvaluate resources for FG", "为 FG 忽略 ValidUntilEvaluate 资源" },
{ "Ignore tagging calls that lack Hudless resource for FG", "为 FG 忽略缺少 Hudless 资源的标记调用" },
{ "Force FG render size motion vectors", "强制 FG 渲染尺寸的运动矢量" },
{ "Create SL on the 2nd device", "在第 2 个设备上创建 SL" },
