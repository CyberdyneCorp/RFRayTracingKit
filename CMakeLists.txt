cmake_minimum_required(VERSION 3.25)
project(RFTraceKit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- Options -----------------------------------------------------------------
option(RFTRACE_BUILD_TESTS    "Build the test suite"          ON)
option(RFTRACE_BUILD_EXAMPLES "Build the example programs"    ON)
option(RFTRACE_ENABLE_PYTHON  "Build the Python bindings"     OFF)
option(RFTRACE_ENABLE_METAL   "Enable the Metal backend"      OFF)
option(RFTRACE_ENABLE_CUDA    "Enable the CUDA/OptiX backend" OFF)
option(RFTRACE_ENABLE_OPENCL  "Enable the OpenCL backend"     OFF)
option(RFTRACE_ENABLE_EMBREE  "Enable the optional Embree CPU backend" OFF)
option(RFTRACE_ENABLE_GDAL    "Enable GDAL-backed GeoTIFF/DEM import & export" OFF)
option(RFTRACE_ENABLE_PARQUET "Enable Apache Arrow/Parquet result export" OFF)

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
  target_link_libraries(rftrace PRIVATE embree)
  target_compile_definitions(rftrace PRIVATE RFTRACE_HAVE_EMBREE=1)
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

# ---- Install -----------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS rftrace EXPORT rftraceTargets
  LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
