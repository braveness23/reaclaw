# Tests — Phase 0 stubs. Real tests added alongside each module.
# Uses GoogleTest fetched via CMake FetchContent.

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
# Prevent GoogleTest from overriding compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ---------------------------------------------------------------------------
# Placeholder — replace with real test targets as modules are implemented
# ---------------------------------------------------------------------------
add_executable(reaclaw_tests
    placeholder_test.cpp
)

target_link_libraries(reaclaw_tests PRIVATE GTest::gtest_main)
target_include_directories(reaclaw_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/
    ${CMAKE_SOURCE_DIR}/vendor/
)

gtest_discover_tests(reaclaw_tests)
