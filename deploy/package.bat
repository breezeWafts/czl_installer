@echo off
setlocal

set QT_DIR=C:\Qt\5.15.2\msvc2019
set BUILD_DIR=build\release
set DEPLOY_DIR=deploy\czl_installer

if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%"

:: 主程序
copy "%BUILD_DIR%\czl_installer.exe" "%DEPLOY_DIR%\" >nul

:: 关键依赖：TIHelper.dll
copy "TIHelper.dll" "%DEPLOY_DIR%\" >nul

:: Qt 依赖
"%QT_DIR%\bin\windeployqt.exe" ^
    --release ^
    --qmldir . ^
    --no-translations ^
    --no-opengl-sw ^
    "%DEPLOY_DIR%\czl_installer.exe"

:: 配置文件
if not exist "%DEPLOY_DIR%\config" mkdir "%DEPLOY_DIR%\config"
xcopy /Y /I config\*.* "%DEPLOY_DIR%\config\" >nul

:: VC 运行时（Win7 需要）
if exist "%VCToolsRedistDir%x64\Microsoft.VC142.CRT\vcruntime140.dll" (
    copy "%VCToolsRedistDir%x64\Microsoft.VC142.CRT\vcruntime140.dll" "%DEPLOY_DIR%\" >nul
    copy "%VCToolsRedistDir%x64\Microsoft.VC142.CRT\msvcp140.dll" "%DEPLOY_DIR%\" >nul
)

echo.
echo 打包完成: %DEPLOY_DIR%
echo.
pause
