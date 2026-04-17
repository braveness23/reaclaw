cmake_minimum_required(VERSION 3.20)
project(reaper_reaclaw VERSION 1.1.0 LANGUAGES C CXX)

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
# Required on Linux/macOS: static lib linked into a shared lib must be PIC
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    src/panel/panel.cpp
    src/panel/swell_stub.cpp
    $<$<BOOL:${WIN32}>:src/panel/panel.rc>
)

target_include_directories(reaper_reaclaw PRIVATE
    src/
    vendor/
    vendor/reaper-sdk/
    vendor/WDL
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
    # Suppress C4505 (unreferenced static function) — reaper_plugin.h BSWAP helpers
    target_compile_options(reaper_reaclaw PRIVATE /wd4505)
    # Suppress C4996 (deprecated) — httplib uses deprecated APIs; fopen vs fopen_s
    target_compile_options(reaper_reaclaw PRIVATE /wd4996)
    # MSVC: suppress min/max macro conflicts with <algorithm>
    target_compile_definitions(reaper_reaclaw PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(reaper_reaclaw PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter    # REAPER callbacks have mandated signatures
        -Wno-cast-function-type  # SWELL stores function pointers as void* in API tables
    )
    # swell-types.h (Linux/macOS) defines min/max macros; suppress them
    # so they don't conflict with std::min / std::max.
    # SWELL_PROVIDED_BY_APP: use REAPER's embedded SWELL instead of compiling
    # GTK ourselves — function pointers are filled at runtime via GetFunc.
    target_compile_definitions(reaper_reaclaw PRIVATE
        NOMINMAX
        SWELL_PROVIDED_BY_APP
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

if(WIN32)
    # cpp-httplib on Windows requires WinSock2, crypt32, and bcrypt
    target_link_libraries(reaper_reaclaw PRIVATE ws2_32 crypt32 bcrypt)
    # Ensure .dll extension (needed for MinGW cross-compile)
    set_target_properties(reaper_reaclaw PROPERTIES SUFFIX ".dll")
    # Statically link MinGW runtime so the DLL is self-contained — no
    # libwinpthread-1.dll / libgcc_s_seh-1.dll / libstdc++-6.dll required
    # on the target machine.
    if(NOT MSVC)
        target_link_options(reaper_reaclaw PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

# Disable tests when cross-compiling (can't run Windows binaries on Linux)
if(CMAKE_CROSSCOMPILING)
    set(REACLAW_BUILD_TESTS OFF)
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
