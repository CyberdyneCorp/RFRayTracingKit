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
  src/scene.cpp
  src/antenna.cpp
  src/backends/cpu_nanort/cpu_backend.cpp
  src/backends/cpu_nanort/raylaunch.cpp
  src/simulator.cpp
  src/importers/mesh_importer.cpp
  src/importers/material_importer.cpp
  src/exporters/json_exporter.cpp
  src/exporters/csv_exporter.cpp
  src/exporters/geojson_exporter.cpp
  src/exporters/gltf_exporter.cpp
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
