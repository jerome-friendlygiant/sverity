# Unit tests: every tests/unit/test_*.cpp is picked up automatically (tiny harness in unit/testing.h).
file(GLOB UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_*.cpp")
add_executable(sverity_unit_tests unit/test_main.cpp ${UNIT_TEST_SOURCES})
target_link_libraries(sverity_unit_tests PRIVATE sverity_lib)
target_include_directories(sverity_unit_tests PRIVATE unit ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(sverity_unit_tests PRIVATE
  SVERITY_SOURCE_DIR="${PROJECT_SOURCE_DIR}" SVERITY_BIN="$<TARGET_FILE:sverity>")

option(SVERITY_CTEST_MEM_GUARD "Wrap CTest commands in tools/dev/mem_guard.sh cgroup limits" ON)
function(sverity_add_test)
  cmake_parse_arguments(ARG "HEAVY" "NAME;PROFILE;SLOT" "COMMAND" ${ARGN})
  if(SVERITY_CTEST_MEM_GUARD)
    add_test(NAME ${ARG_NAME}
             COMMAND bash ${PROJECT_SOURCE_DIR}/tools/dev/mem_guard.sh
                     --profile ${ARG_PROFILE} --label ${ARG_NAME} -- ${ARG_COMMAND})
  else()
    add_test(NAME ${ARG_NAME} COMMAND ${ARG_COMMAND})
  endif()
  if(ARG_HEAVY)
    set_tests_properties(${ARG_NAME} PROPERTIES RESOURCE_LOCK sverity_heavy)
  elseif(DEFINED ARG_SLOT)
    set_tests_properties(${ARG_NAME} PROPERTIES RESOURCE_LOCK "sverity_fixture_slot_${ARG_SLOT}")
  endif()
endfunction()

sverity_add_test(NAME unit PROFILE unit HEAVY COMMAND $<TARGET_FILE:sverity_unit_tests>)

# Fixtures: one ctest entry per rule directory (fixtures/<RULE-ID>), so a single rule can be run
# with `ctest -R fixtures.CDC-003` and light rules can share two guarded slots with `ctest -j`.
add_executable(sverity_fixture_runner unit/fixture_runner.cpp)
target_link_libraries(sverity_fixture_runner PRIVATE sverity_lib)
file(GLOB FIXTURE_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/*")
list(SORT FIXTURE_DIRS)
set(_sverity_heavy_fixtures CDC-004 LNT-012 CDC-018 LNT-009 RDC-004)
set(_sverity_slot 0)
foreach(dir ${FIXTURE_DIRS})
  get_filename_component(name "${dir}" NAME)
  if(IS_DIRECTORY "${dir}" AND NOT name MATCHES "^_")
    if(name IN_LIST _sverity_heavy_fixtures)
      sverity_add_test(NAME fixtures.${name} PROFILE fixture HEAVY
                       COMMAND $<TARGET_FILE:sverity_fixture_runner> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures ${name})
    else()
      sverity_add_test(NAME fixtures.${name} PROFILE fixture SLOT ${_sverity_slot}
                       COMMAND $<TARGET_FILE:sverity_fixture_runner> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures ${name})
      math(EXPR _sverity_slot "(${_sverity_slot} + 1) % 2")
    endif()
  endif()
endforeach()

# Exercises the public corpus gate's failure paths without a network connection.
sverity_add_test(NAME public_projects.gate_self_check PROFILE test HEAVY
                 COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/public_projects/run.sh
                         $<TARGET_FILE:sverity> ${CMAKE_CURRENT_BINARY_DIR}/gate-self-check --group self-check)

sverity_add_test(NAME tutorial.commands PROFILE test
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/docs/tutorial/check.py
                         $<TARGET_FILE:sverity> ${PROJECT_SOURCE_DIR})

sverity_add_test(NAME report.interfaces PROFILE test
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_interfaces.py
                         $<TARGET_FILE:sverity> ${PROJECT_SOURCE_DIR})

# What each rule must have reached: spec block, must-not-fire, mutation pair; everything for a validated rule.
sverity_add_test(NAME rules.conformance PROFILE test
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/dev/conformance.py --check ${PROJECT_SOURCE_DIR})

# The held-out accuracy suite's labels are well formed. Scoring it is a deliberate measurement (tools/dev/accuracy.py), not a test.
sverity_add_test(NAME accuracy.labels PROFILE test
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/dev/accuracy.py --labels-only --suite ${PROJECT_SOURCE_DIR}/tests/accuracy)

# The scorer must reject invalid qualification evidence.
sverity_add_test(NAME accuracy.gate PROFILE test HEAVY
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_accuracy.py $<TARGET_FILE:sverity>)
sverity_add_test(NAME accuracy.cdc_mutation PROFILE test HEAVY
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_cdc_mutation_score.py)
sverity_add_test(NAME accuracy.cdc_reviewed_residuals PROFILE test
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_cdc_reviewed_residuals.py)

# The Verilator policy migrator keeps each directive's scope, and its waivers apply in SVerity (synthetic inputs only).
sverity_add_test(NAME tools.migrate_verilator PROFILE test
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_migrate_verilator.py $<TARGET_FILE:sverity>)

# The noise gate and the Verilator differential read reports; their reconciliation logic runs on synthetic input.
sverity_add_test(NAME tools.replay_tools PROFILE test
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_replay_tools.py)

sverity_add_test(NAME tools.mutation_recall PROFILE test
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_mutation_recall.py)

sverity_add_test(NAME tools.clock_inventory PROFILE test
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_clock_inventory.py
                         $<TARGET_FILE:sverity-clock-inventory>)

sverity_add_test(NAME fixtures.confidence_contract PROFILE fixture HEAVY
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_fixture_confidence.py $<TARGET_FILE:sverity_fixture_runner>)

sverity_add_test(NAME tools.mem_guard PROFILE test HEAVY
                 COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_mem_guard.py $<TARGET_FILE:sverity_fixture_runner>)
