# CLI tools. Each executable links ONLY the public core (rftrace::rftrace) and
# consumes the public API — no new third-party dependency. The version string is
# injected as a compile definition from PROJECT_VERSION (no library symbol).

add_executable(rftrace-cli rftrace_cli.cpp)
add_executable(rftrace-scene-validator scene_validator.cpp)
add_executable(rftrace-result-converter result_converter.cpp)

set(RFTRACE_CLI_TARGETS
    rftrace-cli rftrace-scene-validator rftrace-result-converter)

foreach(tgt IN LISTS RFTRACE_CLI_TARGETS)
  target_link_libraries(${tgt} PRIVATE rftrace::rftrace)
  target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${tgt}
    PRIVATE RFTRACE_CLI_VERSION="${PROJECT_VERSION}")
endforeach()

# ---- Tests -------------------------------------------------------------------
# Reuse the GoogleTest made available by tests/ (RFTRACE_BUILD_TESTS enables the
# tests subdirectory before this one, so the GTest targets already exist). The
# built binary paths are passed as compile definitions so the integration tests
# invoke the real executables cross-generator.
if(RFTRACE_BUILD_TESTS)
  add_executable(rftrace_cli_tests tests/test_cli.cpp)
  target_include_directories(rftrace_cli_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(rftrace_cli_tests
    PRIVATE rftrace::rftrace GTest::gtest GTest::gtest_main)
  target_compile_definitions(rftrace_cli_tests PRIVATE
    RFTRACE_CLI_BIN="$<TARGET_FILE:rftrace-cli>"
    RFTRACE_VALIDATOR_BIN="$<TARGET_FILE:rftrace-scene-validator>"
    RFTRACE_CONVERTER_BIN="$<TARGET_FILE:rftrace-result-converter>")
  add_dependencies(rftrace_cli_tests ${RFTRACE_CLI_TARGETS})

  include(GoogleTest)
  gtest_discover_tests(rftrace_cli_tests
    DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 120)
endif()
