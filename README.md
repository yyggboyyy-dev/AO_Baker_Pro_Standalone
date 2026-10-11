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
