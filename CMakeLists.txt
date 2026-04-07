cmake_minimum_required(VERSION 3.20)
project(reaper_reaclaw VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(REACLAW_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# OpenSSL (system package or vcpkg)
# ---------------------------------------------------------------------------
find_package(OpenSSL REQUIRED)
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "OpenSSL include: ${OPENSSL_INCLUDE_DIR}")

# ---------------------------------------------------------------------------
# cpp-httplib — single header, HTTPS mode requires CPPHTTPLIB_OPENSSL_SUPPORT
# ---------------------------------------------------------------------------
# Vendor: vendor/httplib.h  (yhirose/cpp-httplib, latest release)

# ---------------------------------------------------------------------------
# nlohmann/json — single header
# ---------------------------------------------------------------------------
# Vendor: vendor/json.hpp  (nlohmann/json 3.x)

# ---------------------------------------------------------------------------
# SQLite amalgamation — compiled as a C translation unit
# ---------------------------------------------------------------------------
# Vendor: vendor/sqlite3.c + vendor/sqlite3.h
add_library(sqlite3 STATIC vendor/sqlite3.c)
target_include_directories(sqlite3 PUBLIC vendor/)
target_compile_options(sqlite3 PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
)
# Enable FTS5 (full-text search) for the action catalog
target_compile_definitions(sqlite3 PUBLIC SQLITE_ENABLE_FTS5=1)

# ---------------------------------------------------------------------------
# Main extension target
# ---------------------------------------------------------------------------
add_library(reaper_reaclaw SHARED
    src/main.cpp
    src/server/server.cpp
    src/server/router.cpp
    src/handlers/catalog.cpp
    src/handlers/state.cpp
    src/handlers/execute.cpp
    src/handlers/scripts.cpp
    src/handlers/history.cpp
    src/reaper/api.cpp
    src/reaper/catalog.cpp
    src/reaper/executor.cpp
    src/reaper/scripts.cpp
    src/db/db.cpp
    src/auth/auth.cpp
    src/config/config.cpp
    src/util/tls.cpp
    src/util/logging.cpp
)

target_include_directories(reaper_reaclaw PRIVATE
    src/
    vendor/
    vendor/reaper-sdk/
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(reaper_reaclaw PRIVATE
    sqlite3
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(reaper_reaclaw PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
    REACLAW_VERSION="${PROJECT_VERSION}"
)

# ---------------------------------------------------------------------------
# Platform-specific settings
# ---------------------------------------------------------------------------
set_target_properties(reaper_reaclaw PROPERTIES
    PREFIX ""                          # No "lib" prefix on Linux/macOS
    OUTPUT_NAME "reaper_reaclaw"
    CXX_VISIBILITY_PRESET hidden       # Only export ReaperPluginEntry
    VISIBILITY_INLINES_HIDDEN ON
)

if(MSVC)
    # REAPER on Windows requires MSVC for ABI compatibility.
    target_compile_options(reaper_reaclaw PRIVATE /W4 /WX /permissive-)
    # Suppress C4100 (unreferenced formal parameter) — common in REAPER callbacks
    target_compile_options(reaper_reaclaw PRIVATE /wd4100)
    # MSVC: suppress min/max macro conflicts with <algorithm>
    target_compile_definitions(reaper_reaclaw PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(reaper_reaclaw PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter    # REAPER callbacks have mandated signatures
    )
endif()

if(APPLE)
    # REAPER extensions on macOS are flat bundles — no versioned dylib name
    set_target_properties(reaper_reaclaw PROPERTIES
        SUFFIX ".dylib"
        MACOSX_RPATH OFF
    )
    target_link_libraries(reaper_reaclaw PRIVATE "-framework CoreFoundation")
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
if(DEFINED REAPER_USER_PLUGINS)
    install(TARGETS reaper_reaclaw DESTINATION "${REAPER_USER_PLUGINS}")
    message(STATUS "Install target: ${REAPER_USER_PLUGINS}")
else()
    message(STATUS "REAPER_USER_PLUGINS not set — 'cmake --install' will be a no-op.")
    message(STATUS "Set it with: cmake -DREAPER_USER_PLUGINS=/path/to/UserPlugins")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(REACLAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
