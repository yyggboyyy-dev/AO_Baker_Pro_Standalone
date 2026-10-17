# Windows EXE 自动构建说明

本工程已经配置好 GitHub Actions，可以自动生成点击即用的 Windows EXE。

## 方法一：手动触发构建

1. 把本工程上传到 GitHub 仓库。
2. 打开仓库页面。
3. 点击 `Actions`。
4. 选择 `build-windows-exe`。
5. 点击 `Run workflow`。
6. 等待构建完成。
7. 在构建详情页的 `Artifacts` 下载：

```text
AO_Baker_Pro_Standalone_Windows
```

里面包含：

```text
AO_Baker_Pro_Standalone.exe
AO_Baker_Pro_Standalone_Windows.zip
```

## 方法二：打 tag 自动发布 Release

在本地或 GitHub 网页创建 tag，例如：

```bash
git tag v1.0.0
git push origin v1.0.0
```

Actions 会自动编译，并把以下文件上传到 GitHub Release：

```text
AO_Baker_Pro_Standalone.exe
AO_Baker_Pro_Standalone_Windows.zip
```

## 方法三：本地 Windows 打包

在 `x64 Native Tools Command Prompt for VS 2022` 中运行：

```bat
build_windows_static_exe.bat
```

输出路径：

```text
dist\AO_Baker_Pro_Standalone.exe
```

如需压缩发布包，再运行：

```bat
packaging\package_release.bat
```

## GPU Compute 视口要求

本开发版会优先请求桌面 OpenGL 4.3 Core Profile。运行设备需要安装支持 OpenGL 4.3+ 的显卡驱动才能启用视口 Compute Shader；程序会在视口中显示检测状态。若上下文/着色器不支持，会尝试使用原 CPU 视口路径。当前 GPU Compute 仅用于 3D 视口投影与光栅化，AO 烘焙仍由 CPU 执行。由于当前开发容器缺少 Qt 6 SDK，此版本尚未完成 Windows 编译和 GPU 实机验证。
