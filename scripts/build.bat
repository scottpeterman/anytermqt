@echo off
REM scripts\build.bat
REM
REM Builds everything: the core, the widget, the app and the extension module.
REM Configures first if the build directory is not there yet, so this is the
REM only script needed from a clean checkout.
REM
REM Pass anything through to cmake --build, e.g. a single target:
REM     scripts\build.bat --target anytermqt

setlocal
call "%~dp0env.bat" || exit /b 1

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [build] No build directory -- configuring first.
    call "%~dp0configure.bat" || exit /b 1
)

cmake --build "%BUILD_DIR%" %*
if errorlevel 1 exit /b 1

echo [build] OK
echo [build]   scripts\run.bat        native app
echo [build]   scripts\run-py.bat     the same widget from Python
endlocal