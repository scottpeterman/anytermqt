# CMakeLists.txt
cmake_minimum_required(VERSION 3.21)
project(qtpyte LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(QTPYTE_BUILD_TESTS "Build the test suite" ON)
option(QTPYTE_BUILD_APP "Build the host application" ON)

# The emulator. When this project is built from the repository root, the core
# is already in the tree and adding it again would collide on the target name;
# standalone, it comes from a sibling checkout. Both are supported so the
# widget can be developed on its own without the root build getting in the way.
if(NOT TARGET pyte::core)
    set(QTPYTE_PYTE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pyte"
        CACHE PATH "Path to the pyte emulation core")
    if(NOT EXISTS "${QTPYTE_PYTE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "pyte not found at ${QTPYTE_PYTE_DIR}. Set -DQTPYTE_PYTE_DIR=<path>.")
    endif()
    # Tests and tools are switched off -- this consumes the library, it does
    # not re-verify it. The root build tests the core itself.
    set(PYTE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PYTE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${QTPYTE_PYTE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/pyte)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

# One backend is compiled, chosen here rather than with #ifdefs inside shared
# functions. A POSIX pty and a ConPTY are different enough in shape -- one
# bidirectional descriptor against a pair of pipe handles plus a reader thread
# -- that interleaving them would make both halves harder to read and neither
# easier to test.
if(WIN32)
    set(QTPYTE_PLATFORM_SOURCES src/ptysession_windows.cpp)
else()
    set(QTPYTE_PLATFORM_SOURCES src/pty_posix.cpp src/ptysession_posix.cpp)
endif()

add_library(qtpyte_core
    src/keymap.cpp
    src/palette.cpp
    src/ptysession.cpp
    src/selection.cpp
    src/terminalwidget.cpp
    ${QTPYTE_PLATFORM_SOURCES}
    # Headers are listed so AUTOMOC sees them: it looks for a same-named
    # header beside each source, and these live under include/.
    include/qtpyte/keymap.h
    include/qtpyte/palette.h
    include/qtpyte/pty.h
    include/qtpyte/ptysession.h
    include/qtpyte/selection.h
    include/qtpyte/terminalwidget.h
)
add_library(qtpyte::core ALIAS qtpyte_core)

target_include_directories(qtpyte_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qtpyte_core PUBLIC Qt6::Widgets pyte::core)

# forkpty lives in libutil on Linux; it is in libc on macOS and BSD.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(qtpyte_core PRIVATE util)
endif()

# The ConPTY backend reads on its own thread.
find_package(Threads REQUIRED)
target_link_libraries(qtpyte_core PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(qtpyte_core PRIVATE /W4)
else()
    target_compile_options(qtpyte_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(QTPYTE_BUILD_APP)
    add_executable(qtpyte-term app/main.cpp)
    target_link_libraries(qtpyte-term PRIVATE qtpyte_core)
endif()

if(QTPYTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()