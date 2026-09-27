# Dock spike (Linux only): throwaway test, not part of the ReaClaw build.
#   cmake -S spikes/dock-webview -B build-dockspike && cmake --build build-dockspike
cmake_minimum_required(VERSION 3.20)
project(dockspike C CXX)

set(CMAKE_CXX_STANDARD 17)
set(VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor)

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
