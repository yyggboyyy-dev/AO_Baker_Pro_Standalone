@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0..
set DIST=%ROOT%\dist
set EXE=%DIST%\AO_Baker_Pro_Standalone.exe
set ZIP=%DIST%\AO_Baker_Pro_Standalone_Windows.zip

if not exist "%EXE%" (
    echo [ERROR] 未找到 %EXE%
    echo 请先运行 build_windows_static_exe.bat
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%EXE%' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 exit /b 1

echo [DONE] 发布包已生成：%ZIP%
endlocal
