@echo off
REM scripts\env.bat
REM
REM Every other script starts by calling this one. It settles the four things
REM that vary between machines -- Qt version, Qt root, site-packages, repo
REM root -- and then checks the four things that are wrong most often, before
REM anything slow has run.
REM
REM Called with `call`, not run, so the variables survive into the caller.
REM That is also why there is no setlocal here.
REM
REM Override anything by setting it before the call:
REM     set "QT_VERSION=6.10.4" && scripts\build.bat

if not defined QT_VERSION set "QT_VERSION=6.10.3"
if not defined QT_ROOT    set "QT_ROOT=C:\Qt"
if not defined QT_ARCH    set "QT_ARCH=msvc2022_64"

set "QT_DIR=%QT_ROOT%\%QT_VERSION%\%QT_ARCH%"
set "REPO_ROOT=%~dp0.."
set "BUILD_DIR=%REPO_ROOT%\build"

REM --- 1. the right command prompt -------------------------------------------
REM
REM Not a plain cmd, and not the x86 one. The x86 prompt fails much later with
REM a message about Qt6 not being found "version 6.x.x (64bit)", which names a
REM version and means an architecture.

where cl >nul 2>&1
if errorlevel 1 (
    echo [env] cl.exe not found.
    echo [env] Open the "x64 Native Tools Command Prompt for VS 2022" and retry.
    exit /b 1
)

cl 2>&1 | findstr /c:"for x64" >nul
if errorlevel 1 (
    echo [env] cl.exe is not the x64 compiler -- this is the x86 prompt.
    echo [env] Open the "x64 Native Tools Command Prompt for VS 2022" and retry.
    exit /b 1
)

REM --- 2. Qt is where we think it is -----------------------------------------

if not exist "%QT_DIR%\bin\Qt6Core.dll" (
    echo [env] No Qt at %QT_DIR%
    echo [env] Install it, or set QT_VERSION / QT_ROOT / QT_ARCH before calling.
    echo [env]   python -m aqt install-qt windows desktop %QT_VERSION% %QT_ARCH% --archives qtbase -O %QT_ROOT%
    exit /b 1
)

REM --- 3. where PySide6 and Shiboken actually landed --------------------------
REM
REM A `pip install --user` puts them in the user site directory while sysconfig
REM still reports the machine-wide one, so ask the package itself rather than
REM asking Python where packages go.

for /f "delims=" %%i in ('python -c "import shiboken6_generator,os;print(os.path.dirname(os.path.dirname(shiboken6_generator.__file__)))" 2^>nul') do set "SHIBOKEN_SITELIB=%%i"

if not defined SHIBOKEN_SITELIB (
    echo [env] shiboken6_generator is not importable.
    echo [env]   pip install pyside6==%QT_VERSION% shiboken6==%QT_VERSION% shiboken6_generator==%QT_VERSION%
    exit /b 1
)

REM --- 4. one Qt in the process, not two -------------------------------------
REM
REM The module links the Qt above; PySide6 loads its own and wins at import
REM time. When the two are different builds the result is a crash on import
REM rather than an error, so it is worth ten milliseconds to check here.

for /f "delims=" %%i in ('python -c "from PySide6.QtCore import qVersion; print(qVersion())" 2^>nul') do set "PYSIDE_QT=%%i"

if not defined PYSIDE_QT (
    echo [env] PySide6 is not importable.
    echo [env]   pip install pyside6==%QT_VERSION% shiboken6==%QT_VERSION% shiboken6_generator==%QT_VERSION%
    exit /b 1
)

if not "%PYSIDE_QT%"=="%QT_VERSION%" (
    echo [env] Qt version mismatch: building against %QT_VERSION%, PySide6 carries %PYSIDE_QT%.
    echo [env] These must match or importing the module will crash.
    echo [env]   pip install pyside6==%QT_VERSION% shiboken6==%QT_VERSION% shiboken6_generator==%QT_VERSION%
    exit /b 1
)

echo [env] Qt         %QT_DIR%
echo [env] PySide6 Qt %PYSIDE_QT%
echo [env] sitelib    %SHIBOKEN_SITELIB%
echo [env] build      %BUILD_DIR%
exit /b 0