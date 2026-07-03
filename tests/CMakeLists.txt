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
  test_backend_api.cpp
  test_batched_sim.cpp
  test_bvh.cpp
  test_scene.cpp
  test_georef.cpp
  test_import.cpp
  test_msi_antenna.cpp
  test_rf.cpp
  test_simulation.cpp
  test_results.cpp
  test_golden.cpp
  test_phase2_sim.cpp
  test_raylaunch.cpp
  test_coverage.cpp
  test_coverage_multipath.cpp
  test_geojson.cpp
  test_geo_import.cpp
  test_osm_xml.cpp
  test_osm_pbf.cpp
  test_geotiff.cpp
  test_gltf.cpp
  test_czml.cpp
  test_tiles3d.cpp
  test_tiles3d_lod.cpp
  test_parquet.cpp
  test_phase2_golden.cpp
  test_phase2_threading.cpp
  test_phase3_backend_reuse.cpp
  test_phase2_raylaunch_batch.cpp
  test_receiver_grid.cpp
  test_phase7_regression.cpp
  test_diffraction.cpp
  test_multiedge_diffraction.cpp
  test_utd.cpp
  test_rf_wiring.cpp
  test_polarization.cpp
  test_doppler.cpp
  test_attenuation.cpp
  test_arrays.cpp
  test_mimo.cpp
  test_sinr.cpp
  test_route.cpp
  test_phase7_integration.cpp
  test_metal.cpp
  test_metal_parity.cpp
  test_opencl_parity.cpp
  test_cuda_parity.cpp
)
# nlohmann_json (GeoJSON tests) and assimp (glTF re-import test) are used
# directly by the test sources.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
target_link_libraries(rftrace_tests
  PRIVATE rftrace::rftrace GTest::gtest GTest::gtest_main
          nlohmann_json::nlohmann_json assimp::assimp)

# test_geotiff.cpp uses GDAL directly (to author a synthetic DEM and re-read the
# exported heatmap). RFTRACE_HAVE_GDAL propagates from rftrace's PUBLIC compile
# definition; the test target still needs GDAL's own headers/link when enabled.
if(RFTRACE_ENABLE_GDAL)
  find_package(GDAL CONFIG REQUIRED)
  target_link_libraries(rftrace_tests PRIVATE GDAL::GDAL)
endif()

# test_parquet.cpp uses Arrow/Parquet directly (to re-read the exported table).
# RFTRACE_HAVE_PARQUET propagates from rftrace's PUBLIC compile definition; the
# test target still needs Arrow/Parquet's own headers/link when enabled.
if(RFTRACE_ENABLE_PARQUET)
  find_package(Arrow CONFIG REQUIRED)
  find_package(Parquet CONFIG REQUIRED)
  target_link_libraries(rftrace_tests PRIVATE
    Arrow::arrow_shared Parquet::parquet_shared)
endif()

# test_osm_pbf.cpp uses libosmium directly (to author a synthetic .osm.pbf with
# osmium's writer and read it back). RFTRACE_HAVE_OSMIUM propagates from
# rftrace's PUBLIC compile definition; the test target still needs osmium's
# header paths and the codec libraries the PBF I/O links against.
if(RFTRACE_ENABLE_OSMIUM)
  find_path(OSMIUM_INCLUDE_DIR osmium/io/pbf_input.hpp
    HINTS /opt/homebrew/include /usr/local/include)
  find_path(PROTOZERO_INCLUDE_DIR protozero/version.hpp
    HINTS /opt/homebrew/include /usr/local/include)
  find_library(OSMIUM_EXPAT_LIB NAMES expat
    HINTS /opt/homebrew/opt/expat/lib /usr/local/opt/expat/lib)
  find_library(OSMIUM_LZ4_LIB NAMES lz4
    HINTS /opt/homebrew/opt/lz4/lib /usr/local/opt/lz4/lib)
  find_library(OSMIUM_ZLIB_LIB NAMES z)
  find_library(OSMIUM_BZ2_LIB NAMES bz2)
  target_include_directories(rftrace_tests PRIVATE
    ${OSMIUM_INCLUDE_DIR} ${PROTOZERO_INCLUDE_DIR})
  foreach(lib IN ITEMS
      ${OSMIUM_EXPAT_LIB} ${OSMIUM_LZ4_LIB} ${OSMIUM_ZLIB_LIB} ${OSMIUM_BZ2_LIB})
    if(lib)
      target_link_libraries(rftrace_tests PRIVATE ${lib})
    endif()
  endforeach()
endif()

include(GoogleTest)
# Enumerate tests at ctest time (not build time) so the build never runs the
# freshly linked binary — more robust across toolchains/generators.
gtest_discover_tests(rftrace_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 120)
