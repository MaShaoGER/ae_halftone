# 中文化与多帧渲染验证 · 1.1

## 结果

- AE 26.0x67（中文版）、Apple Silicon 8 核 CPU、16 GB 内存。
- 菜单显示“图案生成 → 有序图案”，16 个插件控件和图形选项使用中文。内部 match name、参数顺序和持久化 ID 未改变。
- 原工程修改前后的同一帧 PNG SHA-256 完全相同：`c5399a5773c47f6eebdecfbc0ad435da6a085f8155dff1c75d2ad994e762f839`。
- 1920×1080、25 fps、24 帧；两个生成器实例，包含内置圆形、自定义星形、外部渐变和旋转/位置关键帧。

| 工程与导出精度 | 关闭多帧：耗时 | 开启多帧：耗时 | 解码后逐帧像素比较 |
|---|---:|---:|---|
| 16 bpc / PNG 16 位 RGBA | 6.273 秒 | 4.628 秒 | 24 / 24 帧完全一致 |
| 8 bpc / PNG 8 位 RGBA | 4.106 秒 | 2.658 秒 | 24 / 24 帧完全一致 |

这是每种设置一次的本机短样例测量，包含 AE 调度、合成和 PNG 编码，不是稳定的通用性能基准。两个任务开始前均清理 AE 缓存。脚本使用 `app.setMultiFrameRenderingConfig()` 仅设置该次渲染，脚本结束后 AE 恢复原来的用户设置。

临时诊断构建记录原生 render 调用的线程及开始/结束时间。16 位多帧阶段观测到 **6 个不同时间帧同时执行**，记录中有 9 个线程 ID；不是只修改标志后推测并行成功。最终发行版不包含这段诊断状态或文件写入代码。

## 内存与线程设计

- `PF_OutFlag2_SUPPORTS_THREADED_RENDERING` 与原有 `REVEALS_ZERO_ALPHA` 同时启用，PiPL 和运行时由同一个配置头生成并静态断言。
- 每次渲染使用独立 Settings、RenderPlan、图形列表及 64 行像素缓冲，不使用可变全局缓存或 sequence_data。
- 图案与渐变直接只读采样 AE checkout 的 8/16 位像素，避免复制整张输入到浮点数组；RAII 保证异常、取消及正常路径均归还图层。
- 4K（3840×2160）单帧浮点中间像素缓冲：`3840×2160×4×4` 字节 → `3840×64×4×4` 字节，即 **126.56 MiB → 3.75 MiB**。此处不计 AE 输出、输入、缓存和布局内存。
- 不额外创建嵌套线程；由 AE 分配帧并行度，并保留定期取消检查。
- 图层行指针使用指针宽度运算，避免大行偏移使用 32 位中间乘积。

## 验证方式

- 核心测试：全帧与分段逐浮点一致、浮点图像与回调采样一致、8 个并发帧与串行一致。
- `scripts/prepare_mfr_validation.jsx` 和 `scripts/render_mfr_validation.jsx` 创建临时验证合成，并分别渲染。测试合成、来源纯色和渲染队列项已在完成后清除。
- `tools/compare_render_frames.cpp` 使用 macOS ImageIO 解码实际 PNG，比较像素哈希和图像尺寸/位深，不依赖压缩文件或时间元数据相同。
- 临时 `--trace` 仅用于诊断；正式构建使用 `python3 scripts/build.py --install`，并检查二进制中无 trace 计数器或日志路径。

## 参考

- [Adobe AE SDK 多帧渲染要求](https://ae-plugins.docsforadobe.dev/effect-details/multi-frame-rendering-in-ae/)
- [AE 脚本渲染配置](https://ae-scripting.docsforadobe.dev/general/application/#appsetmultiframerenderingconfig)
