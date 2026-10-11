@echo off
setlocal enabledelayedexpansion

where cmake >nul 2>nul || (echo [ERROR] 未找到 CMake，请先安装 CMake 并加入 PATH。 & exit /b 1)
where git >nul 2>nul || (echo [ERROR] 未找到 Git，请先安装 Git 并加入 PATH。 & exit /b 1)
where cl >nul 2>nul || (echo [ERROR] 未找到 MSVC cl.exe，请在 x64 Native Tools Command Prompt for VS 2022 中运行本脚本。 & exit /b 1)

set ROOT=%~dp0
set VCPKG_ROOT=%ROOT%third_party\vcpkg

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo [INFO] 正在拉取 vcpkg...
    git clone https://github.com/microsoft/vcpkg "%VCPKG_ROOT%" || exit /b 1
    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics || exit /b 1
)

echo [INFO] 配置 CMake + vcpkg static triplet...
cmake -S "%ROOT%" -B "%ROOT%build" -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DCMAKE_BUILD_TYPE=Release || exit /b 1

echo [INFO] 编译 Release...
cmake --build "%ROOT%build" --config Release --parallel || exit /b 1

echo [INFO] 复制 EXE...
if not exist "%ROOT%dist" mkdir "%ROOT%dist"
copy /Y "%ROOT%build\Release\AO_Baker_Pro_Standalone.exe" "%ROOT%dist\AO_Baker_Pro_Standalone.exe" || exit /b 1

echo [DONE] 已生成：%ROOT%dist\AO_Baker_Pro_Standalone.exe
endlocal
