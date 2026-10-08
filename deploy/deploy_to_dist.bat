@echo off
rem ===========================================================================
rem  deploy_to_dist.bat -- collect czl_installer and ALL runtime dependencies
rem                       into <project root>\dist
rem
rem  Usage (run from the czl_installer directory, or anywhere):
rem      set QT_DIR=D:\Qt\6.12.0\mingw_64
rem      deploy\deploy_to_dist.bat
rem
rem  Optional environment variables:
rem      QT_DIR       Qt kit dir containing bin\windeployqt.exe   (required)
rem      BUILD_DIR    build output dir (auto-detected when omitted)
rem      MINGW_DIR    MinGW toolchain bin (fallback for gcc/stdc++ runtime)
rem      DIST_DIR     output dir, defaults to <project root>\dist
rem
rem  NOTE: this file is intentionally ASCII-only. cmd.exe reads .bat files using
rem        the system ANSI code page (GBK on Chinese Windows); a UTF-8 .bat with
rem        Chinese comments gets mis-parsed by cmd. Keep it ASCII.
rem
rem  Resulting layout:
rem      dist\
rem        czl_installer.exe
rem        TIHelper.dll
rem        Qt6Core.dll / Qt6Gui.dll / Qt6Widgets.dll / Qt6Network.dll ...
rem        platforms\qwindows.dll
rem        styles\ , imageformats\ , tls\ ...
rem        libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll
rem        config\config_ftp.json / config_http.json
rem ===========================================================================
setlocal EnableDelayedExpansion

rem ---------- 0. locate project root (this script lives in <root>\deploy\) ----------
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%.."
set "ROOT=%CD%"
popd

if "%DIST_DIR%"=="" set "DIST_DIR=%ROOT%\dist"

rem ---------- 1. validate QT_DIR ----------
if "%QT_DIR%"=="" (
    echo [ERROR] QT_DIR is not set.
    echo         Example: set QT_DIR=D:\Qt\6.12.0\mingw_64
    exit /b 1
)
if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [ERROR] windeployqt.exe not found under "%QT_DIR%\bin"
    exit /b 1
)

rem ---------- 2. locate build output ----------
rem Candidates are collected from sibling "build-*" directories, NEWEST FIRST.
rem Two sub-paths are probed per directory, covering both layouts:
rem     <dir>\czl_installer.exe             (plain `qmake czl_installer.pro`)
rem     <dir>\czl_installer\czl_installer.exe   (top-level subdirs project)
rem     <dir>\release\czl_installer.exe     (Qt Creator with debug/release split)
rem A Qt5-kit directory is only used when no Qt6-kit directory has an exe,
rem because a stale Qt5 build still runs but links Qt5 DLLs, which would
rem silently ship the wrong binary.
rem
rem NOTE: the scan runs as a subroutine -- a ":label" inside a parenthesised
rem       block is not visible to goto and breaks cmd parsing.
if not "%BUILD_DIR%"=="" goto :build_ready

call :scan_build_dirs
if not "%BUILD_DIR%"=="" goto :build_ready

if exist "%ROOT%\release\czl_installer.exe" set "BUILD_DIR=%ROOT%\release"
if not "%BUILD_DIR%"=="" goto :build_ready
if exist "%ROOT%\debug\czl_installer.exe"   set "BUILD_DIR=%ROOT%\debug"

:build_ready

if "%BUILD_DIR%"=="" (
    echo [ERROR] czl_installer.exe not found. Build first, or set BUILD_DIR.
    exit /b 1
)
if not exist "%BUILD_DIR%\czl_installer.exe" (
    echo [ERROR] "%BUILD_DIR%\czl_installer.exe" does not exist.
    exit /b 1
)

rem If we fell back to a Qt5 kit, say so loudly -- that is almost never intended.
echo %BUILD_DIR% | findstr /I /C:"Qt_5" /C:"Qt5" >nul
if not errorlevel 1 (
    echo [WARN] auto-detected build uses a Qt5 kit:
    echo        %BUILD_DIR%
    echo        If you have a Qt6 build, pass it explicitly: set BUILD_DIR=...
)

goto :after_build_dir

rem ---- probe one directory for an exe -----------------------------------------
:try_build_dir
if "%~1"=="" goto :eof
if exist "%~1\czl_installer.exe" (
    set "BUILD_DIR=%~1"
    goto :eof
)
if exist "%~1\czl_installer\czl_installer.exe" (
    set "BUILD_DIR=%~1\czl_installer"
    goto :eof
)
if exist "%~1\release\czl_installer.exe" (
    set "BUILD_DIR=%~1\release"
    goto :eof
)
if exist "%~1\czl_installer\release\czl_installer.exe" (
    set "BUILD_DIR=%~1\czl_installer\release"
    goto :eof
)
goto :eof

rem ---- two-pass scan: Qt6 kits first, Qt5 kits only as last resort ------------
:scan_build_dirs
set "QT5_HIT="
for %%P in (qt6 qt5) do (
    for /f "delims=" %%D in ('dir /b /ad /o-d "%ROOT%\..\build-*" 2^>nul') do (
        echo %%D | findstr /I /C:"Qt_5" /C:"Qt5" >nul
        if "%%P"=="qt6" (
            if errorlevel 1 call :try_build_dir "%ROOT%\..\%%D"
        ) else (
            if not errorlevel 1 call :try_build_dir "%ROOT%\..\%%D"
        )
        if not "!BUILD_DIR!"=="" goto :eof
    )
)
goto :eof

:after_build_dir

echo ============================================================
echo  project root : %ROOT%
echo  build output : %BUILD_DIR%
echo  Qt kit       : %QT_DIR%
echo  dist dir     : %DIST_DIR%
echo ============================================================
echo.

rem ---------- 3. clean and create dist ----------
if exist "%DIST_DIR%" (
    echo [1/5] cleaning existing dist
    rmdir /s /q "%DIST_DIR%"
)
mkdir "%DIST_DIR%" || exit /b 1

rem ---------- 4. main exe + TIHelper.dll ----------
echo [2/5] copying main exe and TIHelper.dll
copy /Y "%BUILD_DIR%\czl_installer.exe" "%DIST_DIR%\" >nul || exit /b 1

set "DLL_SRC="
if exist "%ROOT%\TIHelper.dll"     set "DLL_SRC=%ROOT%\TIHelper.dll"
if not defined DLL_SRC if exist "%BUILD_DIR%\TIHelper.dll" set "DLL_SRC=%BUILD_DIR%\TIHelper.dll"
if not defined DLL_SRC (
    echo [ERROR] TIHelper.dll not found.
    echo         Build the TIHelper project first; it copies the dll here
    echo         automatically via QMAKE_POST_LINK.
    exit /b 1
)
copy /Y "!DLL_SRC!" "%DIST_DIR%\" >nul || exit /b 1
echo        TIHelper.dll  ^<- !DLL_SRC!

rem ---------- 5. windeployqt ----------
echo [3/5] deploying Qt6 runtime (windeployqt)
rem  pure Widgets + Network: no QML deployment needed
rem  --compiler-runtime also pulls in the MinGW gcc/stdc++ runtime
"%QT_DIR%\bin\windeployqt.exe" ^
    --release ^
    --no-translations ^
    --no-opengl-sw ^
    --no-system-d3d-compiler ^
    --no-quick-import ^
    --compiler-runtime ^
    --dir "%DIST_DIR%" ^
    "%DIST_DIR%\czl_installer.exe" || exit /b 1

rem ---------- 6. fallback: MinGW runtime ----------
echo [4/5] verifying MinGW runtime
if "%MINGW_DIR%"=="" (
    if exist "%QT_DIR%\..\..\Tools\mingw1310_64\bin" set "MINGW_DIR=%QT_DIR%\..\..\Tools\mingw1310_64\bin"
)
for %%L in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if not exist "%DIST_DIR%\%%L" (
        if defined MINGW_DIR (
            if exist "!MINGW_DIR!\%%L" (
                copy /Y "!MINGW_DIR!\%%L" "%DIST_DIR%\" >nul
                echo        fallback copy %%L
            ) else (
                echo        [WARN] %%L missing and not found in !MINGW_DIR!
            )
        ) else (
            echo        [WARN] %%L missing and MINGW_DIR is not set
        )
    )
)

rem ---------- 7. config files ----------
echo [5/5] copying config files
mkdir "%DIST_DIR%\config" 2>nul
xcopy /Y /I /E "%ROOT%\config" "%DIST_DIR%\config\" >nul || exit /b 1

rem ---------- 8. verify ----------
echo.
echo ============================================================
echo  verifying required files
echo ============================================================
set "MISSING=0"
for %%F in (
    "%DIST_DIR%\czl_installer.exe"
    "%DIST_DIR%\TIHelper.dll"
    "%DIST_DIR%\Qt6Core.dll"
    "%DIST_DIR%\Qt6Gui.dll"
    "%DIST_DIR%\Qt6Widgets.dll"
    "%DIST_DIR%\Qt6Network.dll"
    "%DIST_DIR%\platforms\qwindows.dll"
    "%DIST_DIR%\config\config_ftp.json"
    "%DIST_DIR%\config\config_http.json"
) do (
    if exist %%F (
        echo   [OK]      %%~nxF
    ) else (
        echo   [MISSING] %%F
        set "MISSING=1"
    )
)

echo.
if "%MISSING%"=="1" (
    echo [WARN] required files are missing; the app may fail to start.
    exit /b 1
)

echo deploy finished: %DIST_DIR%
echo run it with  : %DIST_DIR%\czl_installer.exe
echo.
endlocal
