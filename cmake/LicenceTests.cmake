# Licence and branding checks as ctest tests, named with their acceptance IDs (SPEC R-L2 to R-L5,
# AC-L1.1, AC-L2.1, AC-L4.1, AC-L5.1). The checks themselves live in tools/check_licences.py.
#
# Included once from the root CMakeLists.txt:   include(cmake/LicenceTests.cmake)
# Run them alone with:                          ctest --test-dir build -L licence --output-on-failure
include_guard(GLOBAL)

if(NOT VJN6_BUILD_TESTS)
  return()
endif()

find_package(Python3 3.8 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
  message(WARNING "Python 3 not found: the licence tests (AC-L1.1, AC-L2.1, AC-L4.1, AC-L5.1) are not registered")
  return()
endif()

enable_testing()

set(_vjn6_licence_check "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_licences.py"
    --root "${PROJECT_SOURCE_DIR}")

# AC-L1.1: the repository passes the R-L3 check ...
add_test(NAME AC-L1.1_licence_check_passes
         COMMAND ${_vjn6_licence_check} --check gpl)
# ... and the check fails when the GNU GPL title is planted in a first-party file (temporary copy).
add_test(NAME AC-L1.1_licence_check_fails_on_planted_gpl_title
         COMMAND ${_vjn6_licence_check} --self-test)
# AC-L2.1: none of the R-L2 GPL-3 projects is in the tree or fetched by CI.
add_test(NAME AC-L2.1_no_gpl3_sources_in_tree_or_ci
         COMMAND ${_vjn6_licence_check} --check sources)
# AC-L4.1: docs/LICENSES.md contains every R-L4 entry.
add_test(NAME AC-L4.1_licenses_md_lists_every_component
         COMMAND ${_vjn6_licence_check} --check docs)
# AC-L5.1: Roland / JUNO / Juno- only in the two R-L5 texts, in ui/src and plugin/ ...
add_test(NAME AC-L5.1_branding_ui_and_plugin_sources
         COMMAND ${_vjn6_licence_check} --check branding)
set(_vjn6_licence_tests
    AC-L1.1_licence_check_passes AC-L1.1_licence_check_fails_on_planted_gpl_title
    AC-L2.1_no_gpl3_sources_in_tree_or_ci AC-L4.1_licenses_md_lists_every_component
    AC-L5.1_branding_ui_and_plugin_sources)

# ... and in the built plugin metadata (Info.plist, moduleinfo.json, *.rc under the plugin build tree).
# Decided at the end of the top-level directory, so it does not matter where this file is included: the
# test exists only when the plugin target VJN6 was configured (plugin/ skips itself without ui/dist).
# It runs after the build and fails if no metadata file was built.
function(_vjn6_add_plugin_metadata_licence_test)
  if(TARGET VJN6)
    add_test(NAME AC-L5.1_branding_built_plugin_metadata
             COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_licences.py"
                     --root "${PROJECT_SOURCE_DIR}" --check metadata --metadata "${PROJECT_BINARY_DIR}/plugin")
    set_tests_properties(AC-L5.1_branding_built_plugin_metadata PROPERTIES LABELS licence TIMEOUT 300)
  endif()
endfunction()
cmake_language(DEFER CALL _vjn6_add_plugin_metadata_licence_test)

set_tests_properties(${_vjn6_licence_tests} PROPERTIES LABELS licence TIMEOUT 300)
unset(_vjn6_licence_check)
unset(_vjn6_licence_tests)
