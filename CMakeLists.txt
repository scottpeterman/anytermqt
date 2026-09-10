# CMakeLists.txt
#
# Top-level build. Two components, deliberately separable:
#
#   pyte/    the terminal emulation core -- plain C++17, no Qt, no display,
#            no assumption that a screen is attached
#   qtpyte/  a Qt terminal widget that consumes it
#
# The core is the durable half. It is built and tested on its own here, not
# as a byproduct of building the widget, so a change that quietly makes it
# depend on the widget cannot pass.
cmake_minimum_required(VERSION 3.21)
project(terminal_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# The widget needs Qt; the core needs nothing but a C++17 compiler. Both
# platforms build the widget -- POSIX over a pty, Windows over ConPTY.
option(BUILD_WIDGET "Build the Qt terminal widget" ON)

enable_testing()

add_subdirectory(pyte)

if(BUILD_WIDGET)
    # Point the widget at the core in this tree rather than a sibling
    # checkout, and let it skip re-verifying a library this build already
    # tested.
    set(QTPYTE_PYTE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pyte" CACHE PATH "" FORCE)
    add_subdirectory(qtpyte)
endif()
option(BUILD_BINDINGS "Build the Python bindings" OFF)

if(BUILD_BINDINGS)
    if(NOT BUILD_WIDGET)
        message(FATAL_ERROR "BUILD_BINDINGS requires BUILD_WIDGET")
    endif()
    add_subdirectory(bindings)
endif()