@echo off
REM scripts\run.bat
REM
REM Runs the native app. Arguments go to it unchanged:
REM     scripts\run.bat --shell powershell.exe --scrollback 20000
REM
REM The PATH line is what lets the Qt DLLs resolve at run time. A distributable
REM build would use windeployqt instead of borrowing Qt's bin directory.

setlocal
call "%~dp0env.bat" || exit /b 1

set "EXE=%BUILD_DIR%\qtpyte\qtpyte-term.exe"
if not exist "%EXE%" (
    echo [run] %EXE% not built. Run scripts\build.bat
    exit /b 1
)

set "PATH=%QT_DIR%\bin;%PATH%"
"%EXE%" %*
endlocal