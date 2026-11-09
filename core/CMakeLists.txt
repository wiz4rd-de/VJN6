set(VJN6_CORE_SOURCES
  instrument.cpp
  core/core.cpp
  engine/engine.cpp
  juno/dsp.cpp
  juno/chorus.cpp
  state/state.cpp
  files/files.cpp
  panel/display.cpp
  bridge/bridge.cpp
)
add_library(vjn6_core STATIC ${VJN6_CORE_SOURCES})
target_include_directories(vjn6_core PUBLIC include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vjn6_core PUBLIC cxx_std_17)
vjn6_determinism(vjn6_core)
if(NOT MSVC)
  target_compile_options(vjn6_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
set_target_properties(vjn6_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Internal headers for tests, the benchmark and the bridges (test-only interface, §9.4).
add_library(vjn6_core_internal INTERFACE)
target_include_directories(vjn6_core_internal INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vjn6_core_internal INTERFACE vjn6_core)
