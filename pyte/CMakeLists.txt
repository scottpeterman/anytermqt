# CMakeLists.txt
cmake_minimum_required(VERSION 3.21)
project(pyte LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(PYTE_BUILD_TESTS "Build the test suite" ON)
option(PYTE_BUILD_TOOLS "Build the ptdump CLI" ON)

include(FetchContent)

# utf8proc: UTF-8 decoding and the East Asian width table. Vendored at a pinned
# tag rather than found on the system so the three platforms agree.
set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(utf8proc
    GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
    GIT_TAG v2.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(utf8proc)

add_library(pyte_core
    src/rowstore.cpp
    src/screen.cpp
    src/stream.cpp
    src/unicode.cpp
    src/screen_range.cpp

)
add_library(pyte::core ALIAS pyte_core)

# PUBLIC include: consumers inherit the header path.
# PRIVATE utf8proc: it appears only in .cpp files, so consumers never see it.
target_include_directories(pyte_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pyte_core PRIVATE utf8proc)

if(MSVC)
    target_compile_options(pyte_core PRIVATE /W4)
else()
    target_compile_options(pyte_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PYTE_BUILD_TOOLS)
    add_executable(ptdump tools/ptdump/main.cpp)
    target_link_libraries(ptdump PRIVATE pyte_core)
endif()

if(PYTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    # Differential check against reference pyte. The unit tests say the code
    # does what its author thought; this says it does what pyte does, on bytes
    # a real device sent. Registered only when ptdump exists to run captures
    # through, and it reports SKIP rather than failure when reference pyte
    # isn't installed -- that's a missing tool, not a broken port.
    if(PYTE_BUILD_TOOLS)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_FOUND)
            file(GLOB PYTE_CAPTURES
                ${CMAKE_CURRENT_SOURCE_DIR}/*.raw
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/captures/*.raw
            )
            if(PYTE_CAPTURES)
                # Two geometries: a wide one and a short one, because most
                # scrolling bugs only show up when rows are scarce.
                foreach(geom "132x40" "80x24")
                    string(REPLACE "x" ";" _dims ${geom})
                    list(GET _dims 0 _cols)
                    list(GET _dims 1 _rows)
                    add_test(NAME ptdiff_${geom}
                        COMMAND ${Python3_EXECUTABLE}
                            ${CMAKE_CURRENT_SOURCE_DIR}/tools/ptdiff/ptdiff.py
                            --ptdump $<TARGET_FILE:ptdump>
                            -c ${_cols} -r ${_rows}
                            ${PYTE_CAPTURES}
                    )
                    set_tests_properties(ptdiff_${geom} PROPERTIES
                        SKIP_RETURN_CODE 77)
                endforeach()
            endif()
        endif()
    endif()
endif()
