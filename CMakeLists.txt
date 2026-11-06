cmake_minimum_required(VERSION 3.25)
project(VJN6 VERSION 0.1.0 LANGUAGES C CXX)

option(VJN6_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU) and standalone app" ON)
option(VJN6_BUILD_WEB "Build the Emscripten web module (use with emcmake)" OFF)
option(VJN6_BUILD_TESTS "Build the Catch2 test suite and the benchmark" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS deployment target")
endif()

include(cmake/Determinism.cmake)
include(cmake/LicenceTests.cmake)

add_subdirectory(core)

if(VJN6_BUILD_TESTS)
  enable_testing()
  add_subdirectory(third_party/Catch2 EXCLUDE_FROM_ALL)
  add_subdirectory(tests)
  add_subdirectory(tools/bench)
  add_subdirectory(tools/display_table)
endif()

if(VJN6_BUILD_PLUGIN AND NOT EMSCRIPTEN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt")
  add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)
  add_subdirectory(plugin)
endif()

if(VJN6_BUILD_WEB AND EMSCRIPTEN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web/CMakeLists.txt")
  add_subdirectory(web)
endif()
