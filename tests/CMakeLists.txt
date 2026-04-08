# Tests — unit tests for subsystems that do not require a live REAPER instance.
# Uses GoogleTest via CMake FetchContent.

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ---------------------------------------------------------------------------
# Source files that can be compiled without the REAPER SDK.
# These modules only depend on OpenSSL, SQLite, and the C++ stdlib.
# ---------------------------------------------------------------------------
set(REACLAW_TESTABLE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/util/logging.cpp
    ${CMAKE_SOURCE_DIR}/src/config/config.cpp
    ${CMAKE_SOURCE_DIR}/src/db/db.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/auth.cpp
)

# ---------------------------------------------------------------------------
# Unit test executable
# ---------------------------------------------------------------------------
add_executable(reaclaw_tests
    test_config.cpp
    test_db.cpp
    test_auth.cpp
    test_logging.cpp
    ${REACLAW_TESTABLE_SOURCES}
)

target_include_directories(reaclaw_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/
    ${CMAKE_SOURCE_DIR}/vendor/
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(reaclaw_tests PRIVATE
    GTest::gtest_main
    sqlite3
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(reaclaw_tests PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
)

# Suppress warnings in test code from vendor headers
if(NOT MSVC)
    target_compile_options(reaclaw_tests PRIVATE
        -Wno-unused-parameter
    )
endif()

gtest_discover_tests(reaclaw_tests)
