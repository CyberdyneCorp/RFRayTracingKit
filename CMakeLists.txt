cmake_minimum_required(VERSION 3.25)

# Auto-enable the vcpkg manifest feature that pulls Embree when the backend is
# requested. Must run before project() so the vcpkg toolchain (invoked at the
# first project() call) installs the feature's dependency. -DRFTRACE_ENABLE_EMBREE
# is available here as a cache variable when passed on the command line.
if(RFTRACE_ENABLE_EMBREE)
  list(APPEND VCPKG_MANIFEST_FEATURES "embree")
endif()

project(RFTraceKit VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- Options -----------------------------------------------------------------
option(RFTRACE_BUILD_TESTS    "Build the test suite"          ON)
option(RFTRACE_BUILD_EXAMPLES "Build the example programs"    ON)
option(RFTRACE_BUILD_CLI      "Build the CLI tools"           ON)
option(RFTRACE_ENABLE_PYTHON  "Build the Python bindings"     OFF)
option(RFTRACE_ENABLE_C_API   "Build the C API (librftrace_c)" OFF)
option(RFTRACE_ENABLE_METAL   "Enable the Metal backend"      OFF)
option(RFTRACE_ENABLE_CUDA    "Enable the CUDA/OptiX backend" OFF)
option(RFTRACE_ENABLE_OPENCL  "Enable the OpenCL backend"     OFF)
option(RFTRACE_ENABLE_EMBREE  "Enable the optional Embree CPU backend" OFF)
option(RFTRACE_ENABLE_GDAL    "Enable GDAL-backed GeoTIFF/DEM import & export" OFF)
option(RFTRACE_ENABLE_PARQUET "Enable Apache Arrow/Parquet result export" OFF)
option(RFTRACE_ENABLE_OSMIUM  "Enable OSM PBF import via libosmium" OFF)

# ---- Dependencies ------------------------------------------------------------
# Resolved via vcpkg (toolchain file) or a system package manager (e.g. Homebrew)
# through CONFIG-mode find_package.
find_package(Eigen3 CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ---- Core library ------------------------------------------------------------
add_library(rftrace
  src/core/geometry.cpp
  src/core/bvh.cpp
  src/core/footprint.cpp
  src/scene.cpp
  src/antenna.cpp
  src/backends/cpu_nanort/cpu_backend.cpp
  src/backends/cpu_nanort/raylaunch.cpp
  src/simulator.cpp
  src/importers/mesh_importer.cpp
  src/importers/material_importer.cpp
  src/importers/msi_importer.cpp
  src/importers/geo_import_util.cpp
  src/importers/geojson_importer.cpp
  src/importers/cityjson_importer.cpp
  src/importers/osm_importer.cpp
  src/importers/osm_extract.cpp
  src/importers/osm_pbf_importer.cpp
  src/importers/geotiff_terrain.cpp
  src/exporters/json_exporter.cpp
  src/exporters/csv_exporter.cpp
  src/exporters/geojson_exporter.cpp
  src/exporters/gltf_exporter.cpp
  src/exporters/czml_exporter.cpp
  src/exporters/tiles3d_exporter.cpp
  src/exporters/parquet_exporter.cpp
  src/exporters/geotiff_heatmap.cpp
)
add_library(rftrace::rftrace ALIAS rftrace)

# Build position-independent so the static library can be linked into shared
# objects — the Python extension (pybind11_add_module) and the C API shared lib.
# Without this, linking librftrace.a into a .so fails on Linux (relocation
# R_X86_64_PC32 ... cannot be used when making a shared object).
set_target_properties(rftrace PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(rftrace PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(rftrace PUBLIC cxx_std_20)
target_link_libraries(rftrace
  PUBLIC  Eigen3::Eigen
  PRIVATE assimp::assimp nlohmann_json::nlohmann_json
)

if(RFTRACE_ENABLE_EMBREE)
  find_package(embree 4 CONFIG REQUIRED)
  target_sources(rftrace PRIVATE src/backends/embree/embree_backend.cpp)
  target_link_libraries(rftrace PRIVATE embree)
  # PUBLIC so the tests target sees it for the parity test's #if guard, mirroring
  # the CUDA/OpenCL/Metal blocks. The Embree include/link stay PRIVATE.
  target_compile_definitions(rftrace PUBLIC RFTRACE_HAVE_EMBREE=1)
endif()

# ---- GDAL (GeoTIFF/DEM import + heatmap export) ------------------------------
# Optional. The GeoTIFF sources (src/importers/geotiff_terrain.cpp,
# src/exporters/geotiff_heatmap.cpp) are always compiled, but their GDAL-backed
# bodies are guarded by RFTRACE_HAVE_GDAL. When OFF the entry points are still
# defined and throw a clear "built without GDAL" error, and gdalAvailable()
# reports false. RFTRACE_HAVE_GDAL is PUBLIC so guarded headers/tests key off it.
if(RFTRACE_ENABLE_GDAL)
  find_package(GDAL CONFIG REQUIRED)
  target_link_libraries(rftrace PRIVATE GDAL::GDAL)
  target_compile_definitions(rftrace PUBLIC RFTRACE_HAVE_GDAL=1)
endif()

# ---- Apache Arrow / Parquet (per-receiver table export) ----------------------
# Optional. When OFF the Parquet entry point is declared but throws a clear
# "built without Parquet" error and parquetAvailable() reports false.
# RFTRACE_HAVE_PARQUET is PUBLIC so guarded headers/tests can key off it. The
# gated .cpp files are only compiled when ON (added by later tasks).
if(RFTRACE_ENABLE_PARQUET)
  find_package(Arrow CONFIG REQUIRED)
  find_package(Parquet CONFIG REQUIRED)
  target_link_libraries(rftrace PRIVATE Arrow::arrow_shared Parquet::parquet_shared)
  target_compile_definitions(rftrace PUBLIC RFTRACE_HAVE_PARQUET=1)
endif()

# ---- libosmium (OSM PBF import) ----------------------------------------------
# Optional. src/importers/osm_pbf_importer.cpp is always compiled, but its
# osmium-backed body is guarded by RFTRACE_HAVE_OSMIUM. When OFF the entry point
# still throws a clear "built without OSM PBF (osmium)" error and
# osmiumAvailable() reports false. libosmium and protozero are header-only, so
# this only adds include paths plus the codec libraries osmium's PBF reader
# needs (zlib for the standard PBF compression; expat/bzip2/lz4 for the wider
# osmium I/O surface). RFTRACE_HAVE_OSMIUM is PUBLIC so guarded headers/tests
# key off it.
if(RFTRACE_ENABLE_OSMIUM)
  find_path(OSMIUM_INCLUDE_DIR osmium/io/pbf_input.hpp
    HINTS /opt/homebrew/include /usr/local/include)
  find_path(PROTOZERO_INCLUDE_DIR protozero/version.hpp
    HINTS /opt/homebrew/include /usr/local/include)
  if(NOT OSMIUM_INCLUDE_DIR OR NOT PROTOZERO_INCLUDE_DIR)
    message(FATAL_ERROR
      "RFTRACE_ENABLE_OSMIUM=ON requires libosmium (osmium/io/pbf_input.hpp) and "
      "protozero (protozero/version.hpp) headers, which were not found. Install "
      "them (e.g. `brew install libosmium protozero`).")
  endif()

  # Codec libraries osmium's I/O layer links against. zlib and bzip2 ship with
  # the platform SDK on macOS (linked by name); expat and lz4 come from Homebrew.
  find_library(OSMIUM_EXPAT_LIB NAMES expat
    HINTS /opt/homebrew/opt/expat/lib /usr/local/opt/expat/lib)
  find_library(OSMIUM_LZ4_LIB NAMES lz4
    HINTS /opt/homebrew/opt/lz4/lib /usr/local/opt/lz4/lib)
  find_library(OSMIUM_ZLIB_LIB NAMES z)
  find_library(OSMIUM_BZ2_LIB NAMES bz2)

  target_include_directories(rftrace PRIVATE
    ${OSMIUM_INCLUDE_DIR} ${PROTOZERO_INCLUDE_DIR})
  foreach(lib IN ITEMS
      ${OSMIUM_EXPAT_LIB} ${OSMIUM_LZ4_LIB} ${OSMIUM_ZLIB_LIB} ${OSMIUM_BZ2_LIB})
    if(lib)
      target_link_libraries(rftrace PRIVATE ${lib})
    endif()
  endforeach()
  target_compile_definitions(rftrace PUBLIC RFTRACE_HAVE_OSMIUM=1)
endif()

# ---- Metal GPU backend (Apple only) -----------------------------------------
# The Objective-C++ backend is compiled only when explicitly enabled on Apple.
# RFTRACE_HAVE_METAL is PUBLIC so consumers (and the guarded factory header)
# can key off it. The kernel is compiled at runtime via newLibraryWithSource,
# so no offline .metallib step is needed.
if(RFTRACE_ENABLE_METAL AND APPLE)
  enable_language(OBJCXX)
  target_sources(rftrace PRIVATE src/backends/metal/metal_backend.mm)
  set_source_files_properties(src/backends/metal/metal_backend.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_compile_definitions(rftrace PUBLIC RFTRACE_HAVE_METAL=1)
  target_link_libraries(rftrace PRIVATE
    "-framework Metal" "-framework Foundation")
endif()

# ---- OpenCL GPU backend ------------------------------------------------------
# OpenCL 1.2 has no hardware ray tracing, so this backend traverses a custom flat
# BVH with a runtime-compiled OpenCL C kernel. Compiled only when explicitly
# enabled and OpenCL is found. RFTRACE_HAVE_OPENCL is PUBLIC so the guarded
# factory header can key off it. The deprecation macros pin/quiet the OpenCL 1.2
# API on macOS (Apple's OpenCL is deprecated but functional).
if(RFTRACE_ENABLE_OPENCL)
  find_package(OpenCL REQUIRED)
  target_sources(rftrace PRIVATE src/backends/opencl/opencl_backend.cpp)
  target_link_libraries(rftrace PRIVATE OpenCL::OpenCL)
  target_compile_definitions(rftrace PUBLIC RFTRACE_HAVE_OPENCL=1)
  target_compile_definitions(rftrace PRIVATE
    CL_SILENCE_DEPRECATION CL_TARGET_OPENCL_VERSION=120)
endif()

# ---- CUDA / OptiX GPU backend ------------------------------------------------
# Hardware ray tracing on NVIDIA GPUs: OptiX builds the acceleration structure
# and a single optixLaunch intersects each ray batch (device programs in
# cuda_programs.cu are compiled to PTX and loaded at runtime). Compiled only when
# explicitly enabled AND both the CUDA Toolkit and an OptiX SDK are found.
# RFTRACE_HAVE_CUDA is PUBLIC so the guarded factory header can key off it.
#
# UNVERIFIED on non-NVIDIA hosts: with no CUDA Toolkit / OptiX SDK this block
# FATAL_ERRORs at configure time with a clear hint — the expected behaviour on,
# e.g., an Apple host. It has not been compiled or run on this machine.
if(RFTRACE_ENABLE_CUDA)
  find_package(CUDAToolkit REQUIRED)

  # OptiX ships as headers only (the implementation is loaded from the driver at
  # runtime via optix_stubs). Locate it through OptiX_INSTALL_DIR.
  if(NOT DEFINED OptiX_INSTALL_DIR AND DEFINED ENV{OptiX_INSTALL_DIR})
    set(OptiX_INSTALL_DIR "$ENV{OptiX_INSTALL_DIR}")
  endif()
  if(NOT OptiX_INSTALL_DIR OR NOT EXISTS "${OptiX_INSTALL_DIR}/include/optix.h")
    message(FATAL_ERROR
      "RFTRACE_ENABLE_CUDA=ON requires the OptiX SDK, but it was not found. "
      "Set -DOptiX_INSTALL_DIR=/path/to/OptiX-SDK (the directory containing "
      "include/optix.h). CUDA Toolkit: found via find_package(CUDAToolkit). "
      "This backend needs NVIDIA hardware + OptiX and cannot be built on hosts "
      "without them (e.g. Apple/AMD).")
  endif()

  enable_language(CUDA)

  # Device programs -> a single PTX file (one CUDA architecture so exactly one
  # PTX object is produced; tune with -DCMAKE_CUDA_ARCHITECTURES).
  add_library(rftrace_cuda_ptx OBJECT src/backends/cuda/cuda_programs.cu)
  set_target_properties(rftrace_cuda_ptx PROPERTIES
    CUDA_PTX_COMPILATION ON)
  if(NOT CMAKE_CUDA_ARCHITECTURES)
    set_target_properties(rftrace_cuda_ptx PROPERTIES CUDA_ARCHITECTURES 60)
  endif()
  target_include_directories(rftrace_cuda_ptx PRIVATE
    "${OptiX_INSTALL_DIR}/include")

  # Write the compiled PTX path into a generated header the host TU includes.
  # $<TARGET_OBJECTS> on a PTX-compilation target expands to the .ptx file.
  set(RFTRACE_CUDA_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cuda")
  file(GENERATE
    OUTPUT "${RFTRACE_CUDA_GEN_DIR}/rftrace_cuda_ptx_path.h"
    CONTENT "#pragma once\n#define RFTRACE_CUDA_PTX_FILE \"$<TARGET_OBJECTS:rftrace_cuda_ptx>\"\n")

  target_sources(rftrace PRIVATE src/backends/cuda/cuda_backend.cpp)
  add_dependencies(rftrace rftrace_cuda_ptx)
  target_include_directories(rftrace PRIVATE
    "${OptiX_INSTALL_DIR}/include"
    "${RFTRACE_CUDA_GEN_DIR}")
  target_link_libraries(rftrace PRIVATE CUDA::cudart CUDA::cuda_driver)
  target_compile_definitions(rftrace PUBLIC RFTRACE_HAVE_CUDA=1)
endif()

# ---- Tests -------------------------------------------------------------------
if(RFTRACE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- Examples ----------------------------------------------------------------
if(RFTRACE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ---- CLI tools ---------------------------------------------------------------
# Three front-end executables (rftrace-cli, rftrace-scene-validator,
# rftrace-result-converter) that consume only the public API. Behind
# RFTRACE_BUILD_CLI (default ON); flag-off leaves the build + suite unchanged.
if(RFTRACE_BUILD_CLI)
  add_subdirectory(cli)
endif()

# ---- Python bindings ---------------------------------------------------------
if(RFTRACE_ENABLE_PYTHON)
  # Locate the interpreter first and have pybind11 use the modern FindPython
  # (respects Python_EXECUTABLE); this avoids stale include paths baked into some
  # distribution pybind11 configs.
  set(PYBIND11_FINDPYTHON ON)
  find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

  # pybind11: prefer a CONFIG-mode package (its cmake dir is on CMAKE_PREFIX_PATH);
  # fall back to a pinned FetchContent, mirroring the GoogleTest style.
  find_package(pybind11 CONFIG QUIET)
  if(NOT pybind11_FOUND)
    include(FetchContent)
    FetchContent_Declare(pybind11
      GIT_REPOSITORY https://github.com/pybind/pybind11.git
      GIT_TAG        v2.13.6
    )
    FetchContent_MakeAvailable(pybind11)
  endif()

  pybind11_add_module(_native bindings/python/pybind11_module.cpp)
  target_link_libraries(_native PRIVATE rftrace::rftrace)

  # Drop the compiled extension next to the pure-Python package so tests run with
  # PYTHONPATH=<repo>/bindings/python.
  set(RFTRACE_PY_PKG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/rftracekit)
  set_target_properties(_native PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${RFTRACE_PY_PKG_DIR}
  )
  # Multi-config generators use per-config subdirectories; pin those too.
  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER ${cfg} cfg_upper)
    set_target_properties(_native PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} ${RFTRACE_PY_PKG_DIR}
    )
  endforeach()
endif()

# ---- C API (stable extern "C" ABI over the core) ----------------------------
# Additive and OFF by default so the default build is unchanged. Builds
# librftrace_c (links the core), installs the C header, and registers a C-only
# test compiled with the C compiler and linked against the C library.
if(RFTRACE_ENABLE_C_API)
  add_library(rftrace_c bindings/c/rftrace_c.cpp)
  add_library(rftrace::rftrace_c ALIAS rftrace_c)
  target_include_directories(rftrace_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
  target_link_libraries(rftrace_c PRIVATE rftrace::rftrace)
  set_target_properties(rftrace_c PROPERTIES C_VISIBILITY_PRESET default)

  install(TARGETS rftrace_c EXPORT rftraceTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
  )

  # The C test is compiled AS C to prove the header is C-compatible.
  enable_language(C)
  enable_testing()
  add_executable(c_api_test tests/c_api_test.c)
  set_target_properties(c_api_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_link_libraries(c_api_test PRIVATE rftrace_c)
  add_test(NAME c_api_test COMMAND c_api_test)
endif()

# ---- Install + export (find_package(RFTraceKit)) -----------------------------
# Produces an installable package with a generated RFTraceKitConfig.cmake so
# downstream projects can `find_package(RFTraceKit)` and link `rftrace::rftrace`
# (and `rftrace::rftrace_c` when the C API was built) without vendoring sources
# or wiring include/link paths by hand. The config re-resolves the library's
# transitive CONFIG-mode dependencies (Eigen3, assimp, nlohmann/json).
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS rftrace EXPORT rftraceTargets
  LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Export the target set (the C API target is added to the same set above when
# RFTRACE_ENABLE_C_API is ON) under the rftrace:: namespace, matching the
# in-tree aliases so consumers link the same target names.
install(EXPORT rftraceTargets
  FILE RFTraceKitTargets.cmake
  NAMESPACE rftrace::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RFTraceKit)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RFTraceKitConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/RFTraceKitConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RFTraceKit)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/RFTraceKitConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/RFTraceKitConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/RFTraceKitConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RFTraceKit)
