@echo off
REM scripts\run-py.bat
REM
REM Runs a script against the freshly built module. Defaults to
REM test_terminal.py; pass another to run that instead:
REM     scripts\run-py.bat bindings\widget_smoke_test.py
REM
REM Note the quoting on every set. Without it, `set VAR=value && ...` puts the
REM space before the && inside the value, and Qt goes looking for a platform
REM plugin named "offscreen " -- with the trailing space, which the error
REM message renders almost invisibly.

setlocal
call "%~dp0env.bat" || exit /b 1

set "SCRIPT=%~1"
if "%SCRIPT%"=="" set "SCRIPT=%REPO_ROOT%\test_terminal.py"
if not "%~1"=="" shift

if not exist "%BUILD_DIR%\bindings\anytermqt.pyd" (
    dir /b "%BUILD_DIR%\bindings\anytermqt*.pyd" >nul 2>&1
    if errorlevel 1 (
        echo [run-py] No anytermqt*.pyd under %BUILD_DIR%\bindings
        echo [run-py] Run scripts\build.bat
        exit /b 1
    )
)

REM PySide6 is imported first and its Qt directory wins regardless of PATH, so
REM this line is for the module's own dependencies rather than for Qt. The
REM version check in env.bat is what makes having both harmless.
set "PATH=%QT_DIR%\bin;%PATH%"
set "PYTHONPATH=%BUILD_DIR%\bindings"

echo [run-py] %SCRIPT%
python "%SCRIPT%" %*
endlocal