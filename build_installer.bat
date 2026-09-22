@echo off
setlocal

REM ============================================================
REM  Organic Chemistry - Windows Release Installer Builder
REM  Version : 1.0.0
REM  Output  : dist\OrganicChemistry_Setup_1.0.0_x64.exe
REM ============================================================

set "APP_NAME=Organic Chemistry"
set "APP_VERSION=1.0.0"
set "SCRIPT_DIR=%~dp0"
set "ISS_FILE=%SCRIPT_DIR%organic_chemistry_installer.iss"
set "DIST_DIR=%SCRIPT_DIR%dist"
set "OUTPUT_EXE=%DIST_DIR%\OrganicChemistry_Setup_%APP_VERSION%_x64.exe"

echo ============================================================
echo  %APP_NAME% Installer Builder  v%APP_VERSION%  (Release)
echo ============================================================

if not exist "%ISS_FILE%" (
  echo [ERROR] 未找到安装脚本: "%ISS_FILE%"
  exit /b 1
)

REM 自动探测 VST3 顶层目录（即包含 "Organic Chemistry.vst3" bundle 的父目录）。
REM 注意：传给 ISCC 用「相对脚本目录的路径」，避免项目路径含空格时
REM 命令行参数被按空格拆分（相对路径 cmake-build-ninja\... 不含空格）。
set "VST3_DIR="

if exist "%SCRIPT_DIR%cmake-build-release-visual-studio\OrganicChemistry_artefacts\Release\VST3\Organic Chemistry.vst3" (
  set "VST3_DIR=cmake-build-release-visual-studio\OrganicChemistry_artefacts\Release\VST3"
)

if not defined VST3_DIR if exist "%SCRIPT_DIR%cmake-build-ninja\OrganicChemistry_artefacts\Release\VST3\Organic Chemistry.vst3" (
  set "VST3_DIR=cmake-build-ninja\OrganicChemistry_artefacts\Release\VST3"
)

if not defined VST3_DIR if exist "%SCRIPT_DIR%cmake-build-release\OrganicChemistry_artefacts\Release\VST3\Organic Chemistry.vst3" (
  set "VST3_DIR=cmake-build-release\OrganicChemistry_artefacts\Release\VST3"
)

if not defined VST3_DIR if exist "%LOCALAPPDATA%\Programs\Common\VST3\Organic Chemistry.vst3" (
  set "VST3_DIR=%LOCALAPPDATA%\Programs\Common\VST3"
)

if not defined VST3_DIR (
  echo [ERROR] 未找到 VST3 构建产物 "Organic Chemistry.vst3"。
  echo [HINT] 请先以 Release 模式构建 Organic Chemistry 后再打包，例如:
  echo        cmake -B cmake-build-ninja -DCMAKE_BUILD_TYPE=Release
  echo        cmake --build cmake-build-ninja
  exit /b 1
)

if not exist "%DIST_DIR%" (
  mkdir "%DIST_DIR%"
)

set "ISCC_PATH=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC_PATH%" set "ISCC_PATH=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC_PATH%" set "ISCC_PATH=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"

if not exist "%ISCC_PATH%" (
  echo [ERROR] 未找到 ISCC.exe，请先安装 Inno Setup 6。
  echo 你可以运行: winget install --id JRSoftware.InnoSetup -e
  exit /b 1
)

echo [INFO] 编译器: "%ISCC_PATH%"
echo [INFO] 安装脚本: "%ISS_FILE%"
echo [INFO] VST3 产物: "%VST3_DIR%"

echo [INFO] 开始打包 ...
echo ------------------------------------------------------------

"%ISCC_PATH%" /DVST3_DIR="%VST3_DIR%" "%ISS_FILE%"

if errorlevel 1 (
  echo ------------------------------------------------------------
  echo [ERROR] 打包失败，请查看上方日志。
  exit /b 1
)

echo ------------------------------------------------------------
if exist "%OUTPUT_EXE%" (
  echo [OK] 打包完成
  echo      输出: "%OUTPUT_EXE%"
) else (
  echo [OK] 打包完成
  echo      输出目录: "%DIST_DIR%"
)
endlocal

echo.
echo 按任意键退出...
pause >nul
exit /b 0
