# bindings/CMakeLists.txt
#
# The Python bindings. Palette proved the toolchain; TerminalWidget is the
# reason the toolchain exists. Adding a class is a matter of extending the
# typesystem and the generated source list, not of changing this file's
# shape.
#
# Requires Shiboken 6.10 or later for the Shiboken6Tools CMake package. On
# earlier versions this had to be several hundred lines of manual generator
# invocation, include-path assembly and platform conditionals; that is why
# the version floor is worth paying.

find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development)

# Shiboken6, Shiboken6Tools and PySide6 ship inside the Python packages, so
# CMake is told where site-packages is rather than being given a prefix by
# hand. Build backends such as scikit-build-core set this themselves; doing
# it here keeps a plain `cmake -S . -B build` working too.
#
# Two candidates, not one. A machine-wide Python whose install directory is
# not writable -- C:\Program Files\Python310 is the usual case -- makes pip
# fall back to a --user install, which lands in the user site directory while
# sysconfig still reports the machine-wide one as purelib. Checking only
# purelib finds nothing there and leaves the failure to be discovered later.
if(NOT DEFINED SHIBOKEN_SITELIB)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c
                "import sysconfig; print(sysconfig.get_path('purelib'))"
        OUTPUT_VARIABLE _sitelib_system
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c
                "import site; print(site.getusersitepackages())"
        OUTPUT_VARIABLE _sitelib_user
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    foreach(_candidate "${_sitelib_system}" "${_sitelib_user}")
        if(EXISTS "${_candidate}/shiboken6_generator")
            set(SHIBOKEN_SITELIB "${_candidate}")
            break()
        endif()
    endforeach()

    if(NOT DEFINED SHIBOKEN_SITELIB)
        # Nothing found. Carry on with purelib so the find_package calls
        # below produce their own diagnostics rather than being pre-empted
        # by a guess about which of the two was meant.
        set(SHIBOKEN_SITELIB "${_sitelib_system}")
    endif()
endif()

message(STATUS "SHIBOKEN_SITELIB: ${SHIBOKEN_SITELIB}")

list(APPEND CMAKE_PREFIX_PATH
    "${SHIBOKEN_SITELIB}/shiboken6_generator/lib/cmake"
    "${SHIBOKEN_SITELIB}/shiboken6/lib/cmake"
    "${SHIBOKEN_SITELIB}/PySide6/lib/cmake")

# Qt's imported targets are directory-scoped: the one qtpyte/ found is not
# visible here.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

# Shiboken6Tools ships CMake files; the generator itself is an imported
# executable target those files define.
find_package(Shiboken6Tools REQUIRED)
find_package(Shiboken6 REQUIRED)
find_package(PySide6 REQUIRED)

# Give the imported generator a Debug location.
#
# shiboken_generator_create_binding reads IMPORTED_LOCATION_DEBUG when
# CMAKE_BUILD_TYPE is Debug and IMPORTED_LOCATION_RELEASE otherwise, but the
# wheel ships only a release target file. In a Debug build the debug property
# is therefore unset, get_target_property yields the string
# "shiboken_path-NOTFOUND", and that goes into the generation rule verbatim.
# Configure succeeds; the build fails much later with what reads like a typo
# in a command name.
#
# Pointing the debug property at the release binary is not a compromise. The
# generator is a build tool that reads headers and writes C++: which
# configuration it was itself compiled in has no bearing on what it emits,
# and there is no debug build of it to prefer.
if(TARGET Shiboken6::shiboken6)
    get_target_property(_shiboken_debug Shiboken6::shiboken6
                        IMPORTED_LOCATION_DEBUG)
    get_target_property(_shiboken_release Shiboken6::shiboken6
                        IMPORTED_LOCATION_RELEASE)

    if(NOT _shiboken_debug AND _shiboken_release)
        set_property(TARGET Shiboken6::shiboken6 APPEND PROPERTY
                     IMPORTED_CONFIGURATIONS DEBUG)
        set_property(TARGET Shiboken6::shiboken6 PROPERTY
                     IMPORTED_LOCATION_DEBUG "${_shiboken_release}")
        set(_shiboken_debug "${_shiboken_release}")
    endif()

    if(NOT _shiboken_release AND NOT _shiboken_debug)
        message(FATAL_ERROR
            "Shiboken6::shiboken6 has no imported location. The "
            "shiboken6_generator package appears to be installed without "
            "its executable. Reinstall it, pinned to the same version as "
            "PySide6. See docs/BUILDING.md.")
    endif()

    message(STATUS "Shiboken generator: ${_shiboken_release}")
endif()

# The extension module is a shared object that links the core statically, so
# every object going into it has to be position independent. Without this the
# link fails on Linux with a relocation error that does not name the cause.
set_property(TARGET qtpyte_core PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET pyte_core PROPERTY POSITION_INDEPENDENT_CODE ON)

# Shiboken derives these filenames from the typesystem; they must be listed
# up front because CMake needs to know the outputs before the generator runs.
# If a name here is wrong the build stops with a missing-source error -- check
# what actually landed in ${CMAKE_CURRENT_BINARY_DIR}/anytermqt/ and correct
# the list. Every class added to the typesystem adds one entry.
set(GENERATED_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/anytermqt/anytermqt_module_wrapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/anytermqt/qtpyte_palette_wrapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/anytermqt/qtpyte_ptysession_wrapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/anytermqt/qtpyte_terminalwidget_wrapper.cpp
)

# On macOS with a framework Qt, shiboken cannot find a single Qt header
# without help. Shiboken6ToolsMacros.cmake derives its framework search path
# from Qt6::Core's LOCATION and goes one directory up:
#
#   .../lib/QtCore.framework/Versions/A/QtCore   LOCATION
#   .../lib/QtCore.framework/Versions            what it passes
#   .../lib                                      what clang needs
#
# Framework includes resolve against the directory holding the .framework
# bundle, so <QtCore/QByteArray> means QtCore.framework/Headers/QByteArray
# only when the search path is lib/. Pointed two levels deeper, nothing
# resolves, and the error names the header rather than the search path.
#
# It stays hidden on a Homebrew Qt, which is framework-based but also installs
# a plain include/QtCore tree -- that lands in INTERFACE_INCLUDE_DIRECTORIES,
# the ordinary -I finds the header, and the framework path is never consulted.
# Only an official Qt build, which ships frameworks alone, reaches this.
#
# Passed as an extra option rather than patched upstream: the wrong path is
# still emitted, it simply resolves nothing, and clang moves on to this one.
set(SHIBOKEN_PLATFORM_OPTIONS "")
if(APPLE)
    get_target_property(_qtcore_is_framework Qt6::Core FRAMEWORK)
    if(_qtcore_is_framework)
        get_target_property(_qtcore_location Qt6::Core LOCATION)
        get_filename_component(_qt_version_dir "${_qtcore_location}" DIRECTORY)
        # Versions/A -> Versions -> QtCore.framework -> lib
        get_filename_component(_qt_framework_dir "${_qt_version_dir}/../../.."
                               ABSOLUTE)
        if(NOT EXISTS "${_qt_framework_dir}/QtCore.framework")
            message(FATAL_ERROR
                "Derived Qt framework directory ${_qt_framework_dir} does not "
                "contain QtCore.framework. shiboken will not find any Qt "
                "header from there. Check the layout of ${QT_DIR}.")
        endif()
        list(APPEND SHIBOKEN_PLATFORM_OPTIONS
             "--framework-include-paths=${_qt_framework_dir}")
        message(STATUS "Qt frameworks for shiboken: ${_qt_framework_dir}")
    endif()
endif()

shiboken_generator_create_binding(
    EXTENSION_TARGET  anytermqt
    LIBRARY_TARGET    qtpyte_core
    TYPESYSTEM_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/typesystem_anytermqt.xml
    HEADERS           ${CMAKE_CURRENT_SOURCE_DIR}/anytermqt_global.h
    GENERATED_SOURCES ${GENERATED_SOURCES}
    QT_MODULES        Core Gui Widgets
    SHIBOKEN_EXTRA_OPTIONS
        -I${CMAKE_SOURCE_DIR}/qtpyte/include
        -I${CMAKE_SOURCE_DIR}/pyte/include
        ${SHIBOKEN_PLATFORM_OPTIONS}
)
# The generator rule above depends on the global header and the typesystem and
# nothing else. IMPLICIT_DEPENDS is meant to cover the transitive includes, but
# it is honoured only by the Makefile generator -- Ninja and Visual Studio
# ignore it without comment. On those generators a change to the widget's
# public API regenerates nothing: qtpyte_core rebuilds, the app picks the change
# up immediately, and the extension module keeps a wrapper generated from the
# previous header. The symptom is a feature that works in the native binary and
# is absent through the bindings, which points at everything except the build.
#
# Naming the headers explicitly closes that. add_custom_command's APPEND form
# adds DEPENDS to the rule already declared for this OUTPUT; the OUTPUT has to
# match that rule's first output exactly, hence the literal path rather than
# the list. Every header the typesystem reaches through anytermqt_global.h
# belongs here.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/anytermqt/anytermqt_module_wrapper.cpp
    APPEND
    DEPENDS
        ${CMAKE_SOURCE_DIR}/qtpyte/include/qtpyte/terminalwidget.h
        ${CMAKE_SOURCE_DIR}/qtpyte/include/qtpyte/selection.h
        ${CMAKE_SOURCE_DIR}/qtpyte/include/qtpyte/palette.h
        ${CMAKE_SOURCE_DIR}/qtpyte/include/qtpyte/ptysession.h
        ${CMAKE_SOURCE_DIR}/pyte/include/pyte/cell.h
        ${CMAKE_SOURCE_DIR}/pyte/include/pyte/screen.h
        ${CMAKE_SOURCE_DIR}/pyte/include/pyte/stream.h
)
# --- installation -------------------------------------------------------------
#
# Everything below concerns the wheel and has no effect on a development build:
# CMake runs the install step only when asked, and scripts/run-py.sh keeps
# using the copy in the build tree with PYTHONPATH.
#
# The installed module has to find Qt, libpyside6 and libshiboken6 without any
# environment set, on a machine that has no Qt SDK. They are in site-packages,
# at a known offset from where the module lands:
#
#   site-packages/
#   |-- anytermqt/<module>.so     <-- $ORIGIN / @loader_path is this directory
#   |-- PySide6/                  libpyside6, and Qt6*.dll on Windows
#   |   `-- Qt/lib/               libQt6Core.so.6 / QtCore.framework
#   `-- shiboken6/                libshiboken6
#
# So the rpath is relative to the module rather than absolute, and resolves at
# load time to whichever PySide6 the user installed. Windows has no rpath --
# python/anytermqt/__init__.py does this with os.add_dll_directory instead.

if(APPLE)
    set(_anytermqt_rpath
        "@loader_path/../PySide6/Qt/lib"
        "@loader_path/../PySide6"
        "@loader_path/../shiboken6")
elseif(UNIX)
    set(_anytermqt_rpath
        "$ORIGIN/../PySide6/Qt/lib"
        "$ORIGIN/../PySide6"
        "$ORIGIN/../shiboken6")
endif()

if(DEFINED _anytermqt_rpath)
    set_target_properties(anytermqt PROPERTIES
        INSTALL_RPATH "${_anytermqt_rpath}"

        # Off, and this one is load-bearing. Left on, CMake appends the link
        # directories -- the Homebrew or aqtinstall Qt this machine built
        # against -- to the installed rpath as well. The wheel then imports
        # perfectly on the machine that built it and fails on every other one,
        # which is the worst available outcome: a broken artifact that passes
        # its own test.
        INSTALL_RPATH_USE_LINK_PATH OFF

        # The build-tree copy keeps the absolute Qt path from the link line, so
        # scripts/run-py.sh continues to work unchanged.
        BUILD_WITH_INSTALL_RPATH OFF)
endif()

# LIBRARY covers the Python extension on every platform -- it is a MODULE
# library, and CMake classifies those under LIBRARY even on Windows. RUNTIME is
# named anyway so that a future change of library type does not silently
# produce an empty wheel.
install(TARGETS anytermqt
        LIBRARY DESTINATION anytermqt
        RUNTIME DESTINATION anytermqt)