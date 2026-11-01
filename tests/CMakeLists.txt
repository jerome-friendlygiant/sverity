# Unit tests: every tests/unit/test_*.cpp is picked up automatically (tiny harness in unit/testing.h).
file(GLOB UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_*.cpp")
add_executable(sverity_unit_tests unit/test_main.cpp ${UNIT_TEST_SOURCES})
target_link_libraries(sverity_unit_tests PRIVATE sverity_lib)
target_include_directories(sverity_unit_tests PRIVATE unit)
target_compile_definitions(sverity_unit_tests PRIVATE
  SVERITY_SOURCE_DIR="${PROJECT_SOURCE_DIR}" SVERITY_BIN="$<TARGET_FILE:sverity>")
add_test(NAME unit COMMAND sverity_unit_tests)

# Fixtures: one ctest entry per rule directory (fixtures/<RULE-ID>, fixtures/mutations), so a single rule can be run
# with `ctest -R fixtures.CDC-003` and the directories run in parallel with `ctest -j`.
add_executable(sverity_fixture_runner unit/fixture_runner.cpp)
target_link_libraries(sverity_fixture_runner PRIVATE sverity_lib)
file(GLOB FIXTURE_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/*")
foreach(dir ${FIXTURE_DIRS})
  get_filename_component(name "${dir}" NAME)
  if(IS_DIRECTORY "${dir}" AND NOT name MATCHES "^_")
    add_test(NAME fixtures.${name}
             COMMAND sverity_fixture_runner ${CMAKE_CURRENT_SOURCE_DIR}/fixtures ${name})
  endif()
endforeach()
