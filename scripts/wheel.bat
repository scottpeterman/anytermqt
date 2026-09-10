@echo off
REM scripts\wheel.bat
REM
REM Builds a wheel, then installs it into a throwaway virtualenv and imports it
REM from a directory with no source tree in sight.
REM
REM The second half is the point, and more so here than on the other two
REM platforms. Windows resolves DLLs from a search path the process carries at
REM run time, not from anything stored in the .pyd -- so a module that imports
REM in the x64 Native Tools prompt, where Qt is on PATH, tells you nothing
REM about what happens in a plain cmd on a machine with no Qt.
REM
REM     scripts\wheel.bat             build, then verify
REM     scripts\wheel.bat --no-test   build only
REM     scripts\wheel.bat --isolated  let pip resolve its own build dependencies

setlocal EnableDelayedExpansion

set "RUN_TEST=1"
set "ISOLATED=0"
for %%a in (%*) do (
    if /i "%%a"=="--no-test"  set "RUN_TEST=0"
    if /i "%%a"=="--isolated" set "ISOLATED=1"
)

call "%~dp0env.bat"
if errorlevel 1 exit /b 1

if not defined DIST_DIR set "DIST_DIR=%REPO_ROOT%\dist"

REM --- 1. the pin in pyproject.toml is the Qt we are building against ----------
REM
REM The wheel declares an exact PySide6 version and the module links whichever
REM Qt QT_DIR points at. If those disagree the wheel is wrong the moment it is
REM created -- it will install for someone, pull the PySide6 it names, and
REM crash on import against a Qt it was never built for.

REM Parsed by scripts\_wheelcheck.py, not inline here. A `python -c` with a
REM regex in it does not survive cmd's quoting rules -- the backslash-quote
REM sequences are torn apart before Python sees them, and the error names a
REM fragment of the expression rather than the problem. It also means this and
REM wheel.sh read the pin through one implementation instead of two.

for /f "delims=" %%i in ('python "%~dp0_wheelcheck.py" pin "%REPO_ROOT%\pyproject.toml"') do set "PIN=%%i"

if not defined PIN (
    echo [wheel] Could not read the PySide6 pin from pyproject.toml.
    exit /b 1
)

if not "%PIN%"=="%PYSIDE_QT%" (
    echo [wheel] pyproject.toml pins PySide6==%PIN%; this environment has %PYSIDE_QT%.
    echo [wheel] A wheel built here would name a PySide6 it was not built against.
    echo [wheel] Either align the environment:
    echo [wheel]   pip install pyside6==%PIN% shiboken6==%PIN% shiboken6_generator==%PIN%
    echo [wheel] or change the pin in pyproject.toml to %PYSIDE_QT% -- both the
    echo [wheel] dependencies line and the build-system requires block.
    exit /b 1
)

echo [wheel] PySide6 pin %PIN% matches this environment

REM --- 2. build ----------------------------------------------------------------
REM
REM --no-build-isolation by default: this interpreter already has the pinned
REM PySide6, Shiboken and generator, which env.bat checked. Isolation would
REM download all three again into a temporary environment for no gain.
REM --isolated is for reproducing what a CI runner or someone building from an
REM sdist would get.

set "ISOLATION_ARGS=--no-build-isolation"
if "%ISOLATED%"=="1" set "ISOLATION_ARGS="

if "%ISOLATED%"=="0" (
    python -c "import scikit_build_core" >nul 2>&1
    if errorlevel 1 (
        echo [wheel] scikit-build-core is not installed.
        echo [wheel]   pip install scikit-build-core
        echo [wheel] Or pass --isolated to let pip fetch it into a build env.
        exit /b 1
    )
)

if exist "%DIST_DIR%\anytermqt-*.whl" del /q "%DIST_DIR%\anytermqt-*.whl"

REM Qt is handed over as an environment variable rather than as a pip
REM --config-settings. CMake reads CMAKE_PREFIX_PATH from the environment when
REM the cache variable is not set, and scikit-build-core runs cmake as a child
REM process that inherits it -- so this needs nothing of pip. The -C flag would
REM have: it arrived in pip 23.1, and the Python 3.10 here is older than that.
set "CMAKE_PREFIX_PATH=%QT_DIR%"

echo [wheel] Building into %DIST_DIR%
python -m pip wheel "%REPO_ROOT%" --no-deps -w "%DIST_DIR%" %ISOLATION_ARGS%
if errorlevel 1 exit /b 1

set "WHEEL="
for /f "delims=" %%f in ('dir /b /o-d "%DIST_DIR%\anytermqt-*.whl" 2^>nul') do (
    if not defined WHEEL set "WHEEL=%DIST_DIR%\%%f"
)

if not defined WHEEL (
    echo [wheel] pip reported success but no anytermqt wheel is in %DIST_DIR%.
    exit /b 1
)
echo [wheel] Built %WHEEL%

python "%~dp0_wheelcheck.py" contents "%WHEEL%"
if errorlevel 1 exit /b 1

if "%RUN_TEST%"=="0" (
    echo [wheel] OK ^(not verified -- --no-test^)
    exit /b 0
)

REM --- 3. verify it somewhere else ---------------------------------------------
REM
REM A separate interpreter, so nothing this environment has can stand in for
REM something the wheel should have carried, and a working directory outside
REM the repository, so no part of the checkout is importable.
REM
REM Note what is deliberately not done here: nothing adds QT_DIR\bin to PATH.
REM If the import needs it, python\anytermqt\__init__.py is not doing its job
REM and the wheel would fail on any machine without a Qt install.

if not defined WHEEL_TEST_VENV set "WHEEL_TEST_VENV=%BUILD_DIR%\wheel-test-venv"

if not exist "%WHEEL_TEST_VENV%\Scripts\python.exe" (
    echo [wheel] Creating test environment at %WHEEL_TEST_VENV%
    python -m venv "%WHEEL_TEST_VENV%"
    if errorlevel 1 exit /b 1
)

echo [wheel] Installing the wheel ^(PySide6 %PIN% comes with it -- this is slow once^)
"%WHEEL_TEST_VENV%\Scripts\python.exe" -m pip install --quiet --upgrade pip
"%WHEEL_TEST_VENV%\Scripts\python.exe" -m pip install --quiet --force-reinstall "%WHEEL%"
if errorlevel 1 exit /b 1

set "TMPRUN=%TEMP%\anytermqt-wheel-check"
if exist "%TMPRUN%" rd /s /q "%TMPRUN%"
mkdir "%TMPRUN%"

pushd "%TMPRUN%"
set "QT_QPA_PLATFORM=offscreen"
"%WHEEL_TEST_VENV%\Scripts\python.exe" "%~dp0_wheelcheck.py" verify
set "RC=%errorlevel%"
popd
rd /s /q "%TMPRUN%"

if not "%RC%"=="0" (
    echo [wheel] The wheel built but does not import outside the source tree.
    echo [wheel] On Windows that is DLL resolution, not rpath. Check that
    echo [wheel] python\anytermqt\__init__.py imports PySide6 before the
    echo [wheel] extension, and that the Qt in %QT_DIR% is the same version
    echo [wheel] as the one inside PySide6 -- ImportError: DLL load failed is
    echo [wheel] what both look like.
    exit /b 1
)

echo [wheel] OK
exit /b 0