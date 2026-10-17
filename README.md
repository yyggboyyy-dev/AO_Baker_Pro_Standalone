# AO Baker Pro Standalone — C++/Qt + Assimp 静态打包版

这是按参考图结构制作的 Windows 独立 EXE 工程方案：

- GUI：C++ / Qt Widgets
- 模型解析：Assimp 静态链接
- FBX：通过 Assimp 独立解析，不调用 Blender / Maya / 3ds Max 等 DCC
- AO：程序内部 CPU 半球射线采样
- 输出：顶点色 GLB + AO 贴图
- 自动化：已内置 GitHub Actions，可自动生成 Windows EXE Artifact / Release

## 你最终要下载的文件

自动构建完成后，下载：

```text
AO_Baker_Pro_Standalone.exe
```

这是点击即可运行的 Windows 桌面程序。

## 一键生成 EXE 的方式

### GitHub Actions

把项目上传到 GitHub 后，进入：

```text
Actions > build-windows-exe > Run workflow
```

构建完成后，在 Artifacts 中下载：

```text
AO_Baker_Pro_Standalone_Windows
```

### 本地 Windows

在 `x64 Native Tools Command Prompt for VS 2022` 中运行：

```bat
build_windows_static_exe.bat
```

成功后得到：

```text
dist\AO_Baker_Pro_Standalone.exe
```

## 功能清单

1. 导入模型
   - OBJ
   - FBX
   - GLTF / GLB

2. 数据保留
   - 顶点位置
   - 顶点法线
   - UV0
   - 顶点色
   - Mesh 名称
   - 尽量保留光滑组 / 软硬边相关法线结果

3. AO 参数
   - AO 强度
   - 地面反光颜色
   - 地面反光强度
   - AO 过渡范围
   - AO 过渡对比度
   - 最暗处
   - 最亮处
   - 射线数量
   - 最大烘焙距离
   - 细节保留
   - 自适应采样开关

4. 输出
   - AO 烘焙到顶点色：`*_ao_vertex.glb`
   - AO 烘焙到贴图：`*_AO.png/jpg/bmp` + `*_ao_texture.glb`

## 项目结构

```text
CMakeLists.txt
vcpkg.json
build_windows_static_exe.bat
BUILD_EXE_GUIDE.md
.github/workflows/build-windows-exe.yml
packaging/package_release.bat
src/
  main.cpp
  MainWindow.h / MainWindow.cpp
  ModelData.h / ModelData.cpp
  ModelIO.h / ModelIO.cpp
  AOBaker.h / AOBaker.cpp
  TextureRasterizer.h / TextureRasterizer.cpp
  ViewportWidget.h / ViewportWidget.cpp
```

## 商业化增强建议

1. 增加 BVH / Embree 加速。
2. 增加多线程 AO 采样。
3. 增加多 UV Set 完整导入导出。
4. 增加 DirectX / OpenGL 实时 PBR 预览。
5. 增加批处理模式。
6. 增加 FBX 样本兼容性测试集。


## Faogen 功能对标说明

请先阅读 `FAOGEN_PARITY_AUDIT_CN.md`。特别说明：当前贴图路径使用逐纹素 CPU 射线烘焙；GPU 后端尚未实现，界面会明确标记为未实现。逐纹素路径和新增 UV 扩边/射线起点偏移尚待 Windows 目标环境编译和基准模型验证。


## 逐纹素 AO 贴图烘焙（本次新增）

`TextureRasterizer::bakeAOTexture()` 已从“顶点 AO 值插值到 UV”改为真正的逐纹素射线烘焙：

- 将 UV 三角形光栅化到纹理像素，并保存每个覆盖像素的三角形索引与重心坐标。
- 在每个有效纹素处插值世界位置和法线，再沿余弦加权半球方向发射 AO 射线。
- 使用独立 BVH 加速三角形相交查询；采样线程按纹理行分配，支持多线程设置。
- 使用 AO 强度、射线距离、射线起点偏移、采样数、过渡对比、平滑和明暗范围设置。
- UV 岛扩边像素数可配置（0–64 px）。
- 贴图任务不再先执行顶点色 AO；避免重复计算，并且不修改原始场景顶点色。
- 保留现有纹理格式输出与 UV 岛扩边流程。

### 注意事项

- 这是 CPU 多线程逐纹素实现，不是 GPU 烘焙。
- 贴图输出的射线采样数量会显著影响耗时；建议先用 256/512/1024 预览，再提高质量。
- 当前实现对纹理分辨率设置了内存安全限制；极高分辨率请在有足够内存的 Windows 机器上验证。
- 当前执行环境未安装 Qt 6 开发包，无法在此处完成完整 CMake 编译；请在 Windows 的项目依赖环境中编译并用带 UV 的测试模型验证。

## GPU Compute 视口加速（开发版）

- 3D 视口增加 OpenGL 4.3+ Compute Shader 路径：GPU 计算顶点相机投影，随后由 GPU 完成三角形光栅化与深度测试，避免原先逐三角形通过 `QPainter` 绘制的主要开销。
- 启动时检测实际 OpenGL 上下文与 Compute Shader 编译结果；支持时状态栏显示 GPU Compute 状态，不支持或运行失败时回退到原 CPU 绘制路径。
- 当前限制：GPU Compute 仅用于普通 3D 视口的顶点投影；静态顶点位置会缓存到 GPU，预览颜色仍由 CPU 每帧准备并上传；顶点绘制/笔刷命中仍使用 CPU 路径。AO 顶点烘焙与逐纹素 AO 烘焙仍是 CPU 实现，**不能视为 GPU AO 烘焙已完成**。
- 需要桌面 OpenGL 4.3+ 驱动。当前开发容器缺少 Qt 6 开发包，无法在此完成 C++ 编译、Shader 实际编译或显卡运行时验证；请在 Windows 目标机验证后再作为稳定发行版使用。
