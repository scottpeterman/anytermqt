@echo off
REM scripts\clean.bat
REM
REM Removes the build directory outright. The case that needs it is a cached
REM compiler: CMake records which cl.exe it detected, so switching from the x86
REM prompt to the x64 one changes nothing until the cache is gone, and the
REM architecture error survives the fix that should have cured it.
REM
REM For the narrower case -- Python disagreeing with the native app -- use
REM scripts\regen-bindings.bat instead and keep the rest of the build.
REM
REM Does not call env.bat: this has to work when the environment is exactly
REM what is wrong.

setlocal
set "BUILD_DIR=%~dp0..\build"

if not exist "%BUILD_DIR%" (
    echo [clean] Nothing at %BUILD_DIR%
    exit /b 0
)

echo [clean] Removing %BUILD_DIR%
rmdir /s /q "%BUILD_DIR%"
echo [clean] OK -- scripts\build.bat to start over
endlocal