@echo off
REM scripts\regen-bindings.bat
REM
REM Throws away the generated wrapper sources and builds them again.
REM
REM The symptom this exists for: something works in the native binary and is
REM missing or inert through Python. The widget's .cpp files rebuild into
REM qtpyte_core and the app picks the change up at once, while the extension
REM module keeps a wrapper Shiboken generated from an older header -- so the
REM C++ looks correct, because it is.
REM
REM bindings\CMakeLists.txt now names the widget headers as explicit DEPENDS
REM on the generation rule, which is what stops this happening. (The macro's
REM own IMPLICIT_DEPENDS covers it only under the Makefile generator; Ninja
REM and Visual Studio ignore it silently.) So this should be rare. Reach for
REM it after changing a header that is not on that DEPENDS list, or when the
REM Python side disagrees with the app and you want that possibility ruled
REM out in thirty seconds rather than reasoned about.

setlocal
call "%~dp0env.bat" || exit /b 1

set "GEN_DIR=%BUILD_DIR%\bindings\anytermqt"

if exist "%GEN_DIR%" (
    echo [regen] Removing %GEN_DIR%
    rmdir /s /q "%GEN_DIR%"
) else (
    echo [regen] Nothing generated yet at %GEN_DIR%
)

cmake --build "%BUILD_DIR%" --target anytermqt
if errorlevel 1 exit /b 1

echo [regen] OK
endlocal