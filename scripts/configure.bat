@echo off
REM scripts\configure.bat
REM
REM Configures a Release build with the bindings on. Run once; build.bat
REM re-runs CMake by itself whenever a CMakeLists.txt changes.
REM
REM Release, not Debug, and not by preference: PySide6 ships release binaries
REM only, so a Debug extension module wants the debug CRT and python3XX_d.lib
REM and has nothing to link against.

setlocal
call "%~dp0env.bat" || exit /b 1

cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DBUILD_BINDINGS=ON ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DSHIBOKEN_SITELIB="%SHIBOKEN_SITELIB%"

if errorlevel 1 (
    echo.
    echo [configure] Failed. If this ran before against a different compiler or
    echo [configure] architecture, CMake has cached the old one: scripts\clean.bat
    echo [configure] and configure again.
    exit /b 1
)

echo [configure] OK
endlocal