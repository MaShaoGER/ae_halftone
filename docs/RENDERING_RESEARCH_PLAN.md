# 有序图案：渲染参考调研与 macOS / Windows 架构规划

调研日期：2026-10-29。状态：规划，未实施渲染或平台改造。

范围修订：不承担现有插件 / 旧工程兼容，允许重排参数与重设算法。以用户常用的 8 / 16 bpc 工作为主要性能与画质验收目标，32 bpc / HDR 不作为本次专项优化目标。宿主 GPU 接口所需的浮点格式处理仍属于必要能力。

## 1. 推荐方向

采用共享 C++ 核心、共享计算内核和独立宿主后端。macOS 以 Metal 为主；Windows 以 DirectX 12 为优先验证方向，并保留按 AE 实际调度需要接入 CUDA 的位置；两端都有 CPU 参考路径。

Windows 是当前设计的目标平台，不作为 Metal 版本完成后的附加移植。以当前项目使用的 AE 26 为验证基线；首个 Windows 目标为 Windows 11 x64。旧版 AE、Windows ARM64、Intel Mac 不纳入本次目标。

最重要的优化对象是自定义图形的大面积重复采样、透明留白和密集覆盖。粒子运动、物理模拟不是本插件当前的主要问题，不引入与图案生成无关的模拟框架。

## 2. 参考资料及适用范围

### 2.1 Adobe SDK：实际接入 AE 的依据

已检查本机 `.local/sdk/AdobeAfterEffectsSDK_26.5_MacOS/Examples/` 中：

- `Effect/SDK_Invert_ProcAmp/SDK_Invert_ProcAmp.cpp`：设备初始化、SmartRenderGPU、Metal / DirectX / CUDA / OpenCL 分支。
- `Util/DirectXUtils.*`：DirectX 12 设备、命令队列和计算着色器适配。
- `Effect/SDK_Invert_ProcAmp/SDK_Invert_ProcAmp_Kernel.cu`：经 KernelSupport 宏表达的共用计算内核；文件扩展名为 `.cu` 不代表该段算法只能使用 CUDA。
- `Effect/SDK_Invert_ProcAmp/Win/SDK_Invert_ProcAmp.vcxproj`：Windows `.aex` 输出、v143 工具链、HLSL 编译和 DirectXAssets。
- `GPUUtils/PrGPU/KernelSupport/`、`GPUUtils/ParseHLSL.py`：多 GPU 目标的内核包装和生成流程。
- `Headers/AE_ComputeCacheSuite.h`、`Headers/AE_EffectSuites.h`：跨渲染计算缓存与输入状态签名。

关键发现：当前 SDK 的 DirectX 分支明确使用 `ID3D12Device` 和 `ID3D12CommandQueue`。但 SDK 中存在分支，不等于任意 AE 版本、项目设置和显卡都会向插件提供该分支；必须验证真实回调和实际渲染。

学习目标：宿主设备与缓冲的生命周期、像素格式、取消机制、CPU 回退、共享内核和发布资源组织。示例中的逐调用同步等做法仍需性能分析，不能原样当作最终架构。

Adobe 官方运行环境说明：[AE GPU 与驱动要求](https://helpx.adobe.com/after-effects/desktop/get-started/technical-requirements/basics-gpu-after-effects.html)。该说明用于了解宿主环境，插件接口行为以 SDK 和实机为准。

### 2.2 AMD GPUParticles11：密集粒子渲染的源码参考

来源：[AMD 开源仓库](https://github.com/GPUOpen-LibrariesAndSDKs/GPUParticles11)。已检查的版本为 `1f2ef4f0cf2025238c7823eede0ec0db7346efab`。

实际阅读：

- [TiledRendering.hlsl](https://github.com/GPUOpen-LibrariesAndSDKs/GPUParticles11/blob/1f2ef4f0cf2025238c7823eede0ec0db7346efab/gpuparticles11/src/Shaders/TiledRendering.hlsl)：每块候选列表、组共享数据、逐像素前向合成。
- [CoarseCullingCS.hlsl](https://github.com/GPUOpen-LibrariesAndSDKs/GPUParticles11/blob/1f2ef4f0cf2025238c7823eede0ec0db7346efab/gpuparticles11/src/Shaders/CoarseCullingCS.hlsl)：粗粒度空间分桶。
- [ShaderConstants.h](https://github.com/GPUOpen-LibrariesAndSDKs/GPUParticles11/blob/1f2ef4f0cf2025238c7823eede0ec0db7346efab/gpuparticles11/src/Shaders/ShaderConstants.h)：示例使用 32×32 细分块，并在 CPU / HLSL 间共享常量。

值得借鉴：两级筛选、候选数据局部共享、沿正确顺序合成、覆盖量可视化，以及普通图形绘制与计算渲染两条路线。

不能直接沿用：示例每块最多加载 500 个粒子，超出的后方粒子会被舍弃；合成达到阈值时还会提前终止并强制 alpha 为 1。这是游戏样例的画质取舍。最终质量路径需要分批处理完整列表，仅在剩余贡献确实为零时跳过；近似阈值只允许用于明确选择的快速预览。

该项目是旧版 DirectX 11 样例。借鉴算法结构，不将其旧构建环境和硬件要求当作本插件发布目标；示例的分块尺寸也不直接定为跨显卡最优值。

### 2.3 Particular：产品行为与预览策略参考

来源：[Rendering Group](https://help.maxon.net/rg/en-us/Content/html/55-Trapcode-Particular-rendering.html)、[Sprite Controls](https://help.maxon.net/rg/en-us/Content/html/24-Trapcode-Particular-particle-sprite-controls.html)。

可确认的公开行为包括 CPU / GPU 选择、Direct / Streaming 路径、运动预览、降低预览粒子数量，以及同帧素材复用。

学习目标是明确表达速度与精度取舍、复用素材、能力不足时选择正确路径。这些产品文档不能证明它的内部缓存、排序或 GPU 内核具体实现；不据此推断其私有源码。

本项目是规则图案生成器，减少预览图形数量可能误导疏密判断，因此不把随机丢图形作为默认预览策略。

### 2.4 NVIDIA GPU Gems：过度绘制与分辨率策略

来源：[High-Speed, Off-Screen Particles](https://developer.nvidia.cn/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles)。

文章强调屏幕覆盖量与重复覆盖成本，并使用较低分辨率的离屏图降低软粒子开销。它也指出高频细节不适合直接采用这一方法。

对本项目的结论：圆点、细线、星形等锐利图案不能默认低分辨率绘制后放大。该策略仅作为用户明确选择的预览方法，最终渲染保持请求分辨率和约定的采样语义。

### 2.5 Apple：GPU 调度与内存流量

来源：[Optimize Metal Performance for Apple silicon Macs](https://developer.apple.com/videos/play/wwdc2020/10632/)。

学习目标：减少不必要的渲染步骤与中间图读写、避免错误的资源依赖、控制重复覆盖、利用工作负载重叠。

Apple 硬件的分块优势不能直接当作 Windows 显卡也会具有的相同性能。共享算法，分别选择工作组尺寸、批大小与调度参数。

### 2.6 跨平台着色器工具链

- [Microsoft DXC](https://github.com/microsoft/DirectXShaderCompiler)：HLSL 到 DXIL，也支持 SPIR-V 输出。
- [Khronos SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)：从 SPIR-V 生成 Metal 等目标语言，并提供资源反射。
- [Apple Metal shader converter](https://developer.apple.com/metal/shader-converter/)：可从 DXIL 生成 Metal 库，同时存在系统、设备能力和资源绑定约束。

首选先验证 Adobe 随 SDK 提供的共享内核包装，因为它与 AE 示例的资源接口最接近。若难以表达所需的分桶、扫描和组共享操作，再比较 HLSL → DXIL / SPIR-V → MSL 与 HLSL → DXIL → Metal converter。

工具支持转换不等于生成代码已适配 AE 输入输出。验证范围必须包括资源绑定、结构体布局、采样坐标、工作组屏障、精度和实际性能。最终只保留一条主生成链，避免算法在多份手写着色器中漂移。

## 3. 目标架构

```text
AE 参数、输入状态与请求区域
              ↓
共享核心：依赖图 / 布局 / 素材表示 / 空间索引 / 路径选择
              ↓
共享计算内核：筛选 / 素材采样 / 覆盖 / 有序合成
              ↓
宿主后端：Metal | DirectX 12 | 按需 CUDA | CPU 参考
              ↓
AE 输出与缓存归还
```

### 3.1 四条渲染路径

1. **通用有序计算路径**：作为跨平台主路径。每个输出像素只处理空间索引中的候选。重新明确图形顺序、覆盖采样与合成规则，允许优化现有四次采样方式，以新版本的边缘质量和时间稳定性验收。
2. **密集遮挡路径**：从上层向下累积贡献；完全不透明后跳过下方。最终质量不使用接近不透明的近似替代。
3. **稀疏批量绘制路径**：验证硬件光栅化是否比通用计算更快。AE 提供的 GPU 输出可能是缓冲而非可直接作为渲染目标的纹理；需要验证格式、行对齐和资源互操作，否则计入中间纹理转换成本，不宣称天然零拷贝。
4. **专用数学路径**：对已验证的周期布局、内置几何、同色图案分别优化。条件不成立时回到通用路径。单色透射率归约、周期复用等以新版本通用算法为参照验证精度，不要求复现现有 CPU 的逐位结果。

两种通用 GPU 路线要用真实场景对照，不能在实验前断定光栅化或计算着色器一定更快。

### 3.2 素材表示

缓存原始尺寸、有效区域、像素格式、透明 / 不透明区域索引，以及必要的 GPU 资源。跳过留白时保留原图层坐标、图形中心和视觉尺寸。

明确新版本对零 alpha RGB、预乘与非预乘输入的解释，按该规则建立透明区域索引。裁剪和不透明判断需覆盖实际滤波与抗锯齿采样的完整足迹，不保留历史算法的偶然行为。

允许采用预缩放、mipmap、解析抗锯齿、距离场等新采样方式，依据 8 / 16 bpc 下的轮廓、细节、透明度与动画稳定性决定默认方案。快速预览与完整质量之间的差异需明确，不以旧算法像素相同作为约束。

### 3.3 布局与索引

重新定义连续、稳定的排列坐标与二维渐变映射。允许比较前向步进、积分反解和其他布局方法，不承担既有位置的兼容；选择时重点验证间距控制、偏移连续性、旋转和关键帧动画不跳变。

GPU 已持有渐变图时，考虑 GPU 按行生成布局，避免为很小的布局计算读回整张输入图。CPU 输入已经就绪且场景较小时，也可使用 CPU 布局。

分块候选保留稳定图形 ID，不能以原子写入的随机顺序直接合成。巨大图形使用更高层列表或独立通道，避免索引复制膨胀。超长列表分批处理，不截断最终画面。

只绘制 AE 请求区域，但从原锚点推导排列；不因局部请求改变步进相位。

### 3.4 缓存与多帧调度

将素材、布局、覆盖索引和最终结果分别建立依赖与缓存键。素材状态、实际采样时间、布局参数、分辨率、位深、色彩解释、算法版本等按真实依赖纳入键。

不以“图层 ID 相同”推断素材内容相同；不为了生成缓存键每次重新读取并哈希整帧。利用 AE 输入状态接口，并对静态跨帧复用做实际验证。

不透明度属于每个图形的合成输入，修改它通常必须重新合成，不能简单乘在最终图上。RGB 着色只有在明确可分离的情况下才能延后处理。

AE Compute Cache 管理可共享的计算结果；设备资源另按宿主设备身份和生命周期管理，不能假定所有 GPU 资源都可直接交由同一个缓存自动管理。已提交 GPU 的资源在任务完成前不得回收。设置统一的 CPU / GPU 内存预算并约束多帧并发，避免缓存收益被内存压力抵消。

## 4. Windows 从现在开始需要固定的约束

| 项目 | 规划 |
|---|---|
| 共享部分 | 参数与工程格式、布局规则、素材描述、索引算法、合成语义、测试场景 |
| macOS | Apple Silicon + Metal，输出 `.plugin` |
| Windows | 先验证 Windows 11 x64 + DirectX 12，输出 `.aex` |
| NVIDIA | 验证 AE 实际是否提供 DirectX；如果宿主只提供 CUDA，使用 CUDA 适配或正确回退 |
| AMD / Intel | 验证宿主 DirectX 调度及设备能力，不能只凭显卡支持 DX12 宣称可用 |
| CPU | 同一 C++ 参考实现；ARM NEON / x64 SIMD 属于后端细节 |
| 构建 | 规划 CMake 与平台预设；Windows 使用 MSVC / Windows SDK，macOS 使用 Apple 工具链 |
| 资源 | 分离 PiPL 的 Windows 资源编译与 macOS Rez；打包所需着色器及运行资源 |
| 工程与参数 | 按新架构重新组织，不要求延续现有 match name、参数 ID、顺序或旧默认行为；Mac / Windows 新版本使用统一参数定义 |

DirectX 12 是优先验证方案，不是对所有 Windows AE 环境的已验证承诺。CUDA 不作为 Windows 唯一方案，以免把非 NVIDIA 显卡排除。暂不为旧版 AE 增加 OpenCL 后端。

暂不以 Vulkan / WebGPU 统一宿主设备：当前 SDK 枚举中没有对应原生后端，独立设备与 AE 资源间的互操作成本需要另外证明。该选择基于宿主集成与性能，不是为了减少实现工作。

性能与画质资源集中于 8 / 16 bpc。32 bpc 工程、HDR 超白 / 负值及相关工作流不作为本次专项优化与完整支持承诺；未来需要时再单独定义范围。

这不等于删除 GPU 浮点接口。本机 SDK 将 `PF_PixelFormat_GPU_BGRA128` 明确标为每通道 32 位浮点，GPU 示例也按该格式读写。即使最终目标是 8 / 16 bpc 工程，仍须验证并正确处理宿主实际交付的 GPU 格式、通道、步幅和数值范围，不能假定可以直接向该缓冲写入 8 / 16 位整数。

允许对内部资源做混合精度优化，但 AE 的 16 bpc 整数与 FP16 不等价。坐标、覆盖与密集透明度累积是否使用 FP32，由精度和稳定性测试决定；不能为了减少位宽破坏 16 bpc 的有效层次。保留必要的内部 FP32，不代表投入 32 bpc 工程专项优化。

32 bpc 工程的实际可用行为需在接口实验后明确；不宣称已支持，不以静默截断超白或负值的方式冒充完整支持。

## 5. 验证阶段与交付物

### A. 双平台宿主接口验证

后续在 macOS / Windows 的 AE 中运行最小 GPU 效果，确认真正收到的框架、设备、格式和输出。Windows 至少覆盖 NVIDIA、AMD、Intel 三类设备中的目标型号，记录宿主版本与驱动版本。

交付物：实际可用后端矩阵、输入输出约定、一次真实渲染及 CPU 回退证明。仅能编译不算通过。本轮不进行这些实现或实机测试。

### B. 共享核心与结果基线

建立新版本的 CPU 参考算法与画质基线，包括旋转、子像素、半透明彩色、透明留白、图层动画、外部渐变、预览分辨率、边界和多帧渲染，优先覆盖 8 / 16 bpc。当前算法只用于判断改进收益，不作为兼容标准。

交付物：可在两端运行的基准场景、像素差分与计时报告。以新版本算法的明确语义作为基准，GPU / CPU 根据 8 / 16 bpc 输出质量确定误差门槛，不维护旧版渲染分支。

### C. 两类 GPU 绘制器对照

实现并比较批量绘制与分块有序计算。覆盖小图形大量分散、大图形密集覆盖、半透明彩色最坏情况，测候选量、真实采样量、遮挡跳过率、GPU 时间、资源转换和峰值内存。

交付物：依据实测选择路径的规则，而不是按图形数量单独判断快慢。

### D. 分层缓存与专用算法

验证改色、改不透明度、改间距、改尺寸、动画素材各自触发哪些重算。再加入周期、同色和内置几何快速路径，逐项证明适用条件和画面误差。

### E. 产品化验收

两端测试冷缓存、暖缓存、拖动参数、随机跳时间、多帧并发、1080p / 4K 和长时间运行。比较中位数与较慢帧，不只报告最佳帧。

半分辨率交互 16–33 ms、1080p 完整质量 41.7 ms / 帧仍属于目标预算，不能从上一轮 CPU 模拟结果外推成保证。分别报告插件耗时与完整 AE 合成耗时。

## 6. 本轮范围

本轮完成参考资料查阅、指定源码阅读和跨平台规划，新增本文档。未修改正式渲染源码、构建脚本或已安装插件，未改变 AE 场景；未编译或运行任何 Windows 版本。
