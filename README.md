# 有序图案 2.0 开发版 · Ordered Patterns

macOS Apple Silicon 的 After Effects 原生效果插件。独立生成透明背景的规则图案阵列，不分析原画面明暗。渐变改变排列的横向间距，图形大小保持独立。

2.0 首个构建加入分块有序合成、素材有效区域裁剪、完全不透明遮挡跳过和 Metal 计算加速，优先服务 8/16 bpc。当前是通过 CPU 效果入口调用 Metal 的混合实现，AE 输入输出仍为 CPU 图像；尚未接入原生 SmartFX GPU 通路。已在 AE 26 完成 32 帧实际导出、两种位深的多帧像素一致性及半分辨率验证，见 [V2_AE_VALIDATION.md](docs/V2_AE_VALIDATION.md)；首次构建记录见 [V2_BUILD_VALIDATION.md](docs/V2_BUILD_VALIDATION.md)。

## 开发环境

- macOS + Apple Silicon，After Effects 2026。
- Apple Command Line Tools：`clang++`、macOS SDK、`Rez`、`codesign`。
- Adobe After Effects **Plug-in SDK**。SDK 保存在本机 `.local/sdk/`，不随插件源码分发。
- Python 3 用于运行构建脚本。VS Code 是可选编辑器，无需完整版 Xcode。

```sh
python3 scripts/build.py --test
python3 scripts/build.py --test --cpu-only
python3 scripts/build.py --host-test
python3 scripts/build.py --benchmark
python3 scripts/build.py --sdk /absolute/path/to/AfterEffectsSDK
python3 scripts/build.py --install
```

SDK 路径首次成功构建后会保存在 `.local/sdk-path.txt`；也可以设置 `AE_SDK_PATH`。VS Code 的“运行生成任务”及“运行任务”提供对应入口。

构建输出：`dist/OrderedPatterns.plugin`。默认安装到 `~/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/`。更新已有插件时，旧版本会备份至项目的 `.local/backups/`。安装或更新后需重启 AE。

Metal 内核在运行时编译，不要求独立的 `metal` / `metallib` 工具。`--host-test` 直接加载构建出的插件并以模拟 SDK 回调验证像素适配，不等于 AE 实机测试。`--benchmark` 运行模拟素材基准，首次调用与之后的计时分别报告。`--cpu-only` 可构建或测试 CPU 回退版本。

共享核心提供 CMake 入口，可在有 C++17 工具链的 Windows / macOS 环境构建 CPU 库与测试：

```sh
cmake -S . -B build/core -DPATTERN_METAL=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

目前没有 Windows `.aex` 或 DirectX / CUDA 后端。本轮仅在 macOS 验证了这套跨平台核心构建入口，未宣称完成 Windows 实机验证。

## 使用

1. 新建合成和一个与合成同尺寸的纯色层。
2. 给纯色层添加 **图案生成 → 有序图案** 效果。纯色层本身的颜色不参与生成。
3. 选择 `图形`：圆形、方形、菱形，或 `自定义`。
4. `密集端间距` / `稀疏端间距` 控制渐变两端的横向间距；`行间距` 独立控制行距。设置两端间距相同即可得到均匀阵列。
5. `图形大小` 独立控制尺寸；`排列角度` 旋转排列，`图形旋转` 旋转每个图形。`交错程度 (%)` 错开相邻行。
6. `渐变起点 (%)` / `渐变终点 (%)` 控制内置线性渐变范围。交换两端间距可反转疏密方向。
7. 使用自定义图形时，将透明 PNG 或预合成加入当前合成，选择 `自定义图形图层`。图形保持宽高比，尺寸指最长边。白色 `图形颜色` 保留图案本色，其他颜色与图案颜色相乘。无自定义图层时输出透明。
8. 可选的 `渐变轮廓图层` 会按每个排列位置读取图层的二维亮度，黑色对应 `密集端间距`、白色对应 `稀疏端间距`。线性、垂直和径向渐变都会在各行的对应位置改变横向间距，行距与图形大小保持独立。来源图层按输出图层尺寸归一化采样；排列旋转和偏移后仍读取实际所在位置的亮度。1.1.1 修复了旧版只读取中心截线、导致径向渐变变成竖向区域的问题。
9. 面板底部新增 `渐变读取强度 (%)`，可设置关键帧，内置渐变与外部渐变图层均适用。`100%` 保持完整渐变效果，`50%` 将渐变造成的间距变化减半，`0%` 按 `密集端间距` 均匀排列。例如两端间距为 20 / 60，强度 50% 时实际间距范围为 20 / 40。图形大小与行距不受影响。默认 100%，旧参数顺序和 ID 均保留。

两端间距可以交换来反转疏密。间距小于图形大小时，图形会重叠；例如方形大小 10、间距 2 会连成横线。

图案和渐变来源层可关闭可见性；选中的图层仍作为效果输入。需要先应用其他效果或变换的图案，请先预合成。自定义素材若有大片透明留白，会影响图案视觉尺寸，建议先裁切。

## 多帧渲染

插件声明多帧渲染支持，由 AE 调度并发帧。2.0 的素材、索引与 GPU 工作缓冲按帧独立，设备、队列与只读计算管线共享；模拟宿主及真实 AE 四路不同时间帧并发均已验证。8 / 16 bpc 下各 8 帧的多帧与串行结果逐像素一致。1.x 的历史 AE 验证见原多帧报告。

CPU 回退仍使用 64 行浮点缓冲。Metal 路径直接生成 ARGB8 / ARGB16 像素，再复制到 AE 输出，不建立整帧 CPU 浮点结果图。4K GPU 输出缓冲约为 31.64 MiB（8 bpc）或 63.28 MiB（16 bpc）；另有素材转换、裁剪素材和分块索引的内存，不能与旧版单项 3.75 MiB 缓冲直接等同。

旧工程的内部 match name 和参数 ID 保持不变。中文菜单入口为“图案生成 → 有序图案”。

## 当前边界

- 优先处理 8/16 bpc，具备 Metal 混合加速与分块 CPU 回退；32 bpc / HDR 专项优化、SmartFX GPU 原生输入输出和跨帧素材缓存尚未实现。
- 内置线性渐变和外部二维亮度图控制横向间距；纵向仍按固定行距排列，没有径向排列、纵向密度变形或图案集切换。
- 图形边缘使用 2×2 采样，极细笔画在小尺寸下可能闪烁。
- 为避免误设造成极大运算量，布局设置了 250,000 个候选位置的保守上限。超限会报错，不会截断画面。请增大间距。
- 尚未验证 Premiere Pro 和 Intel Mac。

## 文件

- `src/PatternCore.*`：与 AE 无关的布局及绘制核心。
- `src/OrderedPatterns.cpp`：AE 参数与像素适配。
- `src/RasterCore.*`、`RasterShared.h`：素材裁剪、稳定空间索引、分块 CPU 合成与跨平台数据布局。
- `src/RasterMetal.*`：运行时编译的 Metal 后端；Windows 目前使用 `RasterGPUStub.cpp` 的 CPU 回退接口。
- `resources/OrderedPatternsPiPL.r`：插件发现资源。
- `scripts/build.py`：命令行构建、签名、本机安装。
- `tests/core_test.cpp`：渐变间距、稳定布局、透明度、等比例图案与取消检查。
- `tools/preview.cpp`：独立算法预览，不代替 AE 验证。

多帧测试详情见 [MFR_VALIDATION.md](docs/MFR_VALIDATION.md)。

1.x 历史版本已在 AE 26.0 中加载并实际渲染，验证了 8/16 bpc 工程、半分辨率预览、旋转关键帧及保存后重新打开。1.1 版额外验证了两种原生位深下各 24 帧的多帧/单帧结果逐像素一致，最多 6 个不同时间帧同时执行。这些记录不代替 2.0 的实机验收。`examples/Ordered Patterns Demo.aep` 包含三个可直接调整的示例。

![AE 实际渲染的自定义星形阵列](docs/ae-custom-star.png)
