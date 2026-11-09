file(GLOB_RECURSE VJN6_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/panel/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/midi/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/files/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/golden/*.cpp)
add_executable(vjn6_tests ${VJN6_TEST_SOURCES})
target_include_directories(vjn6_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vjn6_tests PRIVATE vjn6_core_internal Catch2::Catch2WithMain)
target_compile_definitions(vjn6_tests PRIVATE VJN6_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
vjn6_determinism(vjn6_tests)
add_test(NAME vjn6_tests COMMAND vjn6_tests)

# Engine-only subset (fast iteration on the DSP; the full suite is vjn6_tests).
file(GLOB VJN6_ENGINE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp)
add_executable(vjn6_engine_tests EXCLUDE_FROM_ALL ${VJN6_ENGINE_TEST_SOURCES})
target_include_directories(vjn6_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vjn6_engine_tests PRIVATE vjn6_core_internal Catch2::Catch2WithMain)
target_compile_definitions(vjn6_engine_tests PRIVATE VJN6_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
vjn6_determinism(vjn6_engine_tests)
