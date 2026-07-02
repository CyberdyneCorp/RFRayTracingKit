option(RFTRACE_USE_SYSTEM_GTEST
       "Use an installed GoogleTest instead of fetching a pinned copy" OFF)

if(RFTRACE_USE_SYSTEM_GTEST)
  find_package(GTest CONFIG REQUIRED)
else()
  # Fetch a pinned GoogleTest and build it statically. This keeps the build
  # reproducible across machines and avoids picking up an unrelated system
  # install (e.g. one with an unresolvable dylib rpath).
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(rftrace_tests
  test_geometry.cpp
  test_bvh.cpp
  test_scene.cpp
  test_import.cpp
  test_rf.cpp
  test_simulation.cpp
  test_results.cpp
  test_golden.cpp
  test_phase2_sim.cpp
  test_raylaunch.cpp
  test_coverage.cpp
  test_geojson.cpp
  test_gltf.cpp
  test_phase2_golden.cpp
  test_phase7_regression.cpp
  test_diffraction.cpp
  test_attenuation.cpp
  test_arrays.cpp
  test_mimo.cpp
  test_sinr.cpp
  test_route.cpp
  test_phase7_integration.cpp
  test_metal.cpp
  test_metal_parity.cpp
)
# nlohmann_json (GeoJSON tests) and assimp (glTF re-import test) are used
# directly by the test sources.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
target_link_libraries(rftrace_tests
  PRIVATE rftrace::rftrace GTest::gtest GTest::gtest_main
          nlohmann_json::nlohmann_json assimp::assimp)

include(GoogleTest)
# Enumerate tests at ctest time (not build time) so the build never runs the
# freshly linked binary — more robust across toolchains/generators.
gtest_discover_tests(rftrace_tests DISCOVERY_MODE PRE_TEST)
