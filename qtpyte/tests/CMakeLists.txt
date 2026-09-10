# tests/CMakeLists.txt
find_package(Qt6 6.2 REQUIRED COMPONENTS Test Widgets)

# The widget suite. Four of these start a child process and are written per
# platform; the rest feed bytes straight in and are the same everywhere.
add_executable(qtpyte_tests test_terminal.cpp)
target_link_libraries(qtpyte_tests PRIVATE qtpyte_core Qt6::Test Qt6::Widgets)
add_test(NAME qtpyte_tests COMMAND qtpyte_tests)

# Selection. No pty, no shell, no platform branches -- and fast enough to run
# on every build, which is why it is its own binary rather than more of the
# one above.
add_executable(qtpyte_selection_tests test_selection.cpp)
target_link_libraries(qtpyte_selection_tests PRIVATE qtpyte_core Qt6::Test Qt6::Widgets)
add_test(NAME qtpyte_selection_tests COMMAND qtpyte_selection_tests)

# No display in CI, and none needed: the widget renders to an offscreen
# surface and the tests read pixels and text back from it.
#
# Two caveats when running under ctest on Windows. The offscreen platform has
# no clipboard owner, so the clipboard tests skip themselves; and ctest gives
# the process a redirected pipe rather than a console, which changes where a
# ConPTY child's output lands and fails two of the pty tests for reasons that
# have nothing to do with the library. Run the binaries directly -- or use
# scripts\build-cpp.bat, which does.
set_tests_properties(qtpyte_tests qtpyte_selection_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")