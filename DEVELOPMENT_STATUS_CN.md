# AO Baker Pro — 开发状态（2026-10-16）

## 本次源码审计与修改

- 从 `AO_Baker_Pro_PerTexel_AO_v1.zip` 解包建立开发工作树。
- 检查 CMake、vcpkg 清单、Windows GitHub Actions、AO 射线/BVH、逐纹素 UV 光栅化和 UI 设置持久化代码。
- 新增 `BakeSettings::rayOriginOffset`，按场景包围盒对角线比例计算射线起点偏移，避免偏移随用户设置的最大射线距离不合理变化。
- 新增 `BakeSettings::uvPaddingPixels`，UI 提供 0–64 px UV 岛扩边设置；纳入 QSettings 与项目 JSON 保存/载入。
- 将逐纹素贴图像素安全限制从 32M 降到 20M，降低峰值内存风险。该限制仍是粗略保护，不等于精确内存预算器。
- 将 UI 的 GPU 选项明确标记为“尚未实现”，并在选择时记录 CPU 仍在执行，避免误导。
- 更新 Faogen 对标审计与 README，修正关于旧顶点 AO 插值路径的过期说明。

## 构建验证

运行命令：

```bash
cmake -S . -B build-audit -G Ninja -DCMAKE_BUILD_TYPE=Release
```

结果：配置失败，原因是当前 Linux 审计环境未安装 Qt 6 开发包（缺少 `Qt6Config.cmake`）。这是环境依赖阻塞，不是源码编译成功，也不能据此判定 Windows 构建失败。

## 当前状态（诚实标记）

| 功能 | 状态 |
|---|---|
| 逐纹素 CPU AO 源码 | 已存在，未完成目标平台运行验证 |
| CPU 多线程 | 已存在，未完成性能/正确性基准 |
| BVH 射线查询 | 已存在，未完成大场景压力测试 |
| UV Padding UI/持久化 | 本次已接入，未编译验证 |
| 射线起点偏移 UI/持久化 | 本次已接入，未编译验证 |
| GPU Compute | 未实现 |
| 逐纹素任务取消 | 未实现 |
| UV 重叠诊断 | 未实现 |
| 输出临时文件与原子替换 | 未实现 |
| Windows EXE | 本次未构建 |

## 下一步技术路线

1. 在 Windows + MSVC + Qt 6 + vcpkg 环境完成 Release 构建，并修复实际编译错误。
2. 加入自动化测试模型：封闭盒、凹槽、平面、重叠 UV、无 UV、退化三角形；验证 AO 数值、UV 扩边与输出格式。
3. 将逐纹素烘焙拆为可取消的任务，使用原子取消令牌；先写临时输出文件，成功后替换目标文件。
4. 加入 UV 重叠检测与诊断报告，明确重叠 UV 采用覆盖、平均或报错的策略。
5. 定义 CPU/GPU 统一后端接口。GPU 优先评估 Vulkan Compute（跨厂商）并提供能力探测、显存预算和 CPU 回退；只有 GPU kernel 实际运行并通过结果对比后才标记为可用。
6. 建立基准场景，对比 CPU 线程数、射线数、分辨率、耗时、峰值内存与 AO 差异。


## 2026-10-17：GPU Compute 视口加速首版

### 已实现的代码路径
- 新增 `GpuViewportRenderer`，要求桌面 OpenGL 4.3+。Compute Shader 将 CPU 提供的三角形顶点记录转换为裁剪空间坐标，输出 SSBO；顶点/片元 Shader 从 SSBO 读取数据并执行 GPU 光栅化。
- 主程序优先请求 OpenGL 4.3 Core Profile；视口初始化检测版本、Compute Shader 和缓冲区，失败时保留 CPU `QPainter` 回退。
- 普通视口渲染走 GPU 路径，顶点绘制模式走兼容 CPU 路径；界面叠加文字、坐标轴和网格仍由 Qt 绘制。

### 仍未完成/未验证
- 当前是 GPU 视口 Compute/光栅化，不是 GPU AO 烘焙；AO 烘焙仍为 CPU。
- 静态顶点位置已在视口缓存中复用并以静态缓冲上传；预览颜色仍由 CPU 每帧准备并上传，AO 预览等模式下大场景仍可能受 CPU 数据准备影响。
- 本容器缺少 Qt6 开发包，CMake 配置在 `find_package(Qt6)` 阶段失败，未完成 C++ 编译、Compute Shader 编译、GPU 实机帧率测试。
- 需要在 Windows + OpenGL 4.3+ 设备上验证上下文创建、SSBO、线框状态、顶点绘制回退、DPI 缩放及性能；不应将此开发版标记为已通过运行时测试。


## Windows CI 编译错误修复（2026-10-17）

根据 MSVC 17.14 的 CI 日志修复：

- `TextureRasterizer.cpp` 原先将 `std::thread` 放进 `QVector`。Qt 容器在相关操作中要求元素可复制，而 `std::thread` 不可复制，导致 C2280。现改为 `std::vector<std::thread>`，显式启动并 join 线程。
- 删除 CMake 命令行中未被项目使用的 `VCPKG_BUILD_TYPE`。该变量不是本项目的 CMake 选项；vcpkg triplet 继续由 `VCPKG_TARGET_TRIPLET=x64-windows-static` 控制。
- 此修复针对日志中的编译错误；必须重新运行 Windows CI 才能确认是否还有后续编译或链接错误。

## V4 截图反馈修复（2026-10-17）

用户反馈 V3 在 OpenGL 4.3 Compute 已显示启用的情况下视口只显示网格、模型不可见，且右侧 AO 设置控件出现垂直挤压/叠压。

修复：
- GPU 渲染器创建、绑定并释放 Core Profile 必需的 VAO。原代码仅调用 `glDrawArrays` 而未绑定 VAO，在 Core Profile 下会触发 `GL_INVALID_OPERATION`，这是模型不可见的主要原因。
- GPU 绘制前明确关闭 scissor、启用深度写入并清理深度缓冲，减少与 QPainter / QOpenGLWidget 状态交互造成的绘制问题。
- 右侧设置列改为 `QScrollArea`，面板按自然高度布局，窗口高度不足时滚动而不是压缩 `QFormLayout` 行。
- AO 表单增加行间距、长标签换行；滑块行设定最小高度；设置列保留 390px 内容宽度，狭窄时可水平滚动。

验证边界：源码静态检查与 ZIP 完整性检查；当前环境没有完整 Qt 6/MSVC Windows 构建环境，因此 GPU 实际运行和 Windows 编译仍需在 CI/目标机验证。
