# Native tests of the plugin and standalone wrapper against the real AudioProcessor (Catch2 v3).
# Links the plugin's shared code; JUCE module objects come from there, headers from the submodule.
file(GLOB VJN6_PLUGIN_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(vjn6_plugin_tests ${VJN6_PLUGIN_TEST_SOURCES})
target_include_directories(vjn6_plugin_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/third_party/JUCE/modules)
target_link_libraries(vjn6_plugin_tests PRIVATE VJN6 vjn6_core_internal Catch2::Catch2)
target_compile_definitions(vjn6_plugin_tests PRIVATE VJN6_SOURCE_DIR="${PROJECT_SOURCE_DIR}" VJN6_UI_DIST="${VJN6_UI_DIST}")
add_test(NAME vjn6_plugin_tests COMMAND vjn6_plugin_tests)
