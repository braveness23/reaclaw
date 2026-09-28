# Dock spike: throwaway test, not part of the ReaClaw build.
#   cmake -S spikes/dock-webview -B build-dockspike && cmake --build build-dockspike
# On Windows, pass the vcpkg toolchain so the webview2 port resolves:
#   cmake -S spikes/dock-webview -B build-dockspike -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake_minimum_required(VERSION 3.20)
project(dockspike C CXX)

set(CMAKE_CXX_STANDARD 17)
set(VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor)

if(WIN32)
    # Windows dock uses real HWNDs (no SWELL translation), so the web view
    # (WebView2) runs in-process — no helper executable needed. See ext_win.cpp.
    add_library(reaper_dockspike SHARED ext_win.cpp)
    set_target_properties(reaper_dockspike PROPERTIES PREFIX "" SUFFIX ".dll")
    target_include_directories(reaper_dockspike SYSTEM PRIVATE ${VENDOR} ${VENDOR}/reaper-sdk)
    target_compile_definitions(reaper_dockspike PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    find_package(unofficial-webview2 CONFIG REQUIRED)
    target_link_libraries(reaper_dockspike PRIVATE unofficial::webview2::webview2)
else()
    # Linux/macOS: the web view runs in a separate helper process (webhost.c,
    # WebKitGTK) because SWELL doesn't forward input to in-process GTK widgets.
    add_library(reaper_dockspike SHARED ext.cpp swell_stub.cpp)
    set_target_properties(reaper_dockspike PROPERTIES PREFIX "")
    target_include_directories(reaper_dockspike SYSTEM PRIVATE ${VENDOR} ${VENDOR}/reaper-sdk ${VENDOR}/WDL)
    target_compile_definitions(reaper_dockspike PRIVATE NOMINMAX)
    target_compile_options(reaper_dockspike PRIVATE -Wall -Wno-unused-parameter)
    target_link_libraries(reaper_dockspike PRIVATE dl)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WEBKIT REQUIRED IMPORTED_TARGET webkit2gtk-4.1 gtk+-3.0)
    add_executable(dockspike-webhost webhost.c)
    target_link_libraries(dockspike-webhost PRIVATE PkgConfig::WEBKIT)
endif()
