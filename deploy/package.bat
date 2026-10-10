@echo off
rem ===========================================================================
rem  package.bat -- legacy packaging script (kept for compatibility)
rem
rem  Prefer deploy\deploy_to_dist.bat, which collects the app and every runtime
rem  dependency into <project root>\dist in one step.
rem
rem  This script only stages a folder that windeployqt has already been run
rem  against. It is kept ASCII-only on purpose: cmd.exe reads .bat files using
rem  the system ANSI code page (GBK on Chinese Windows), so a UTF-8 .bat with
rem  Chinese comments gets mis-parsed.
rem
rem  Usage:
rem      set QT_DIR=D:\Qt\6.12.0\mingw_64
rem      deploy\package.bat
rem ===========================================================================
setlocal

if "%QT_DIR%"=="" (
    echo [ERROR] QT_DIR is not set.
    echo         Example: set QT_DIR=D:\Qt\6.12.0\mingw_64
    exit /b 1
)
if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [ERROR] windeployqt.exe not found under "%QT_DIR%\bin"
    exit /b 1
)

rem this script lives in <root>\deploy\
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%.."
set "ROOT=%CD%"
popd

if "%BUILD_DIR%"=="" set "BUILD_DIR=%ROOT%\release"
set "DEPLOY_DIR=%ROOT%\deploy\czl_installer"

if not exist "%BUILD_DIR%\czl_installer.exe" (
    echo [ERROR] "%BUILD_DIR%\czl_installer.exe" not found. Build Release first.
    exit /b 1
)
if not exist "%ROOT%\TIHelper.dll" (
    echo [ERROR] TIHelper.dll not found in "%ROOT%".
    echo         Build the TIHelper project first.
    exit /b 1
)

echo [1/4] staging %DEPLOY_DIR%
if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%" || exit /b 1

echo [2/4] copying exe and TIHelper.dll
copy /Y "%BUILD_DIR%\czl_installer.exe" "%DEPLOY_DIR%\" >nul || exit /b 1
copy /Y "%ROOT%\TIHelper.dll" "%DEPLOY_DIR%\" >nul || exit /b 1

echo [3/4] deploying Qt6 runtime
rem pure Widgets + Network: no QML deployment needed
"%QT_DIR%\bin\windeployqt.exe" ^
    --release ^
    --no-translations ^
    --no-opengl-sw ^
    --no-system-d3d-compiler ^
    --no-quick-import ^
    --compiler-runtime ^
    "%DEPLOY_DIR%\czl_installer.exe" || exit /b 1

echo [4/6] copying config
if not exist "%DEPLOY_DIR%\config" mkdir "%DEPLOY_DIR%\config"
xcopy /Y /I /E "%ROOT%\config" "%DEPLOY_DIR%\config\" >nul || exit /b 1

echo [5/6] deploying PostgreSQL support (mode 2: --pg)
rem  Qt6Sql.dll comes with windeployqt; the QPSQL driver plugin and libpq do not.
if exist "%QT_DIR%\plugins\sqldrivers\qsqlpsql.dll" (
    if not exist "%DEPLOY_DIR%\sqldrivers" mkdir "%DEPLOY_DIR%\sqldrivers"
    copy /Y "%QT_DIR%\plugins\sqldrivers\qsqlpsql.dll" "%DEPLOY_DIR%\sqldrivers\" >nul
    echo        sqldrivers\qsqlpsql.dll
) else (
    echo        [WARN] qsqlpsql.dll not found under "%QT_DIR%" - --pg will fail.
)

if "%PGSQL_DIR%"=="" (
    if exist "C:\Program Files\PostgreSQL\18\bin\libpq.dll" set "PGSQL_DIR=C:\Program Files\PostgreSQL\18\bin"
)
if "%PGSQL_DIR%"=="" (
    if exist "C:\Program Files\PostgreSQL\17\bin\libpq.dll" set "PGSQL_DIR=C:\Program Files\PostgreSQL\17\bin"
)
if "%PGSQL_DIR%"=="" (
    if exist "C:\Program Files\PostgreSQL\16\bin\libpq.dll" set "PGSQL_DIR=C:\Program Files\PostgreSQL\16\bin"
)
if not "%PGSQL_DIR%"=="" (
    for %%L in (libpq.dll libssl-3-x64.dll libcrypto-3-x64.dll libintl-9.dll libiconv-2.dll) do (
        if exist "%PGSQL_DIR%\%%L" copy /Y "%PGSQL_DIR%\%%L" "%DEPLOY_DIR%\" >nul
    )
) else (
    echo        [WARN] libpq.dll not found - set PGSQL_DIR to the PostgreSQL bin dir.
)

echo [6/6] done

echo.
echo packaged: %DEPLOY_DIR%
echo.
endlocal
