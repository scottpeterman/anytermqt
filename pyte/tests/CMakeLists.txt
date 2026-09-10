# tests/CMakeLists.txt
include(FetchContent)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

add_executable(pyte_tests
    main.cpp
    test_altscreen.cpp
    test_history.cpp
    test_screen.cpp
    test_stream.cpp
    test_range.cpp
    
)
target_link_libraries(pyte_tests PRIVATE pyte_core doctest::doctest)

add_test(NAME pyte_tests COMMAND pyte_tests)
