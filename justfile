# RFTraceKit developer tasks — run `just` (or `just --list`) to see everything.
# Requires: cmake >= 3.25, clang++ or g++, ninja (for the gcc/asan recipes),
# vcpkg (set VCPKG_ROOT; deps resolve from vcpkg.json), and `just` itself.
# Optional: Embree (for the `embree` validation backend recipe).

build_dir := "build"
cxx       := "clang++"
# vcpkg toolchain — set VCPKG_ROOT to build against a vcpkg manifest. When unset,
# the flag is omitted and CMake falls back to system packages (e.g. Homebrew),
# so `just build` works in both environments.
vcpkg_arg := if env_var_or_default("VCPKG_ROOT", "") != "" { "-DCMAKE_TOOLCHAIN_FILE=" + env_var_or_default("VCPKG_ROOT", "") / "scripts/buildsystems/vcpkg.cmake" } else { "" }

# Show the available recipes (default).
default:
    @just --list

# Configure the build (CPU backend). Pass extra cmake flags, e.g.
#   just configure -DRFTRACE_ENABLE_EMBREE=ON
configure *FLAGS:
    cmake -S . -B {{build_dir}} \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER={{cxx}} \
      {{vcpkg_arg}} \
      -DRFTRACE_BUILD_EXAMPLES=ON {{FLAGS}}

# Compile the library, test suite, and examples.
build: configure
    cmake --build {{build_dir}} -j

# Compile only the library.
lib: configure
    cmake --build {{build_dir}} --target rftrace -j

# Run the test suite (unit + golden).
test: build
    ./{{build_dir}}/tests/rftrace_tests
alias unit := test

# Run the test suite through CTest.
ctest: build
    ctest --test-dir {{build_dir}} --output-on-failure

# Build and run every example (each self-verifying / prints its result).
examples: build
    #!/usr/bin/env bash
    set -uo pipefail
    fail=0
    for ex in {{build_dir}}/examples/rftrace_*; do
      [ -x "$ex" ] || continue
      name=$(basename "$ex")
      if out=$("$ex" 2>&1); then echo "✅ $name — $(echo "$out" | tail -1 | xargs)";
      else echo "❌ $name"; echo "$out" | grep -E 'FAIL|ERR'; fail=1; fi
    done
    exit $fail

# Run a single example by stem, e.g. `just example simple_los`.
example NAME: build
    ./{{build_dir}}/examples/rftrace_{{NAME}}

# Configure + build + test with debug symbols and assertions (separate dir).
debug:
    cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER={{cxx}} \
      {{vcpkg_arg}}
    cmake --build build-debug -j
    ./build-debug/tests/rftrace_tests

# Build + test with GCC (separate build dir).
gcc:
    cmake -S . -B build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
      {{vcpkg_arg}}
    cmake --build build-gcc
    ./build-gcc/tests/rftrace_tests

# Build + test under AddressSanitizer / UBSan (separate build dir).
asan:
    cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER={{cxx}} \
      {{vcpkg_arg}} \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    cmake --build build-asan
    ./build-asan/tests/rftrace_tests

# Build with the optional Embree CPU backend and run the suite (validation baseline).
embree:
    cmake -S . -B build-embree -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER={{cxx}} \
      {{vcpkg_arg}} -DRFTRACE_ENABLE_EMBREE=ON
    cmake --build build-embree
    ./build-embree/tests/rftrace_tests

# Build with the Metal GPU backend and run its suite (Apple + Metal only).
# Not part of `ci`; tests skip at runtime when no Metal device is present.
metal:
    cmake -S . -B build-metal \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/homebrew \
      {{vcpkg_arg}} -DRFTRACE_ENABLE_METAL=ON
    cmake --build build-metal -j
    ctest --test-dir build-metal --output-on-failure

# Build with the OpenCL GPU backend and run its suite (needs an OpenCL device).
# Not part of `ci`; the parity tests skip at runtime when no OpenCL device is
# present. Verified on Apple OpenCL 1.2 (M2 Max).
opencl:
    cmake -S . -B build-opencl \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/homebrew \
      {{vcpkg_arg}} -DRFTRACE_ENABLE_OPENCL=ON
    cmake --build build-opencl -j
    ctest --test-dir build-opencl --output-on-failure

# Build with GDAL (GeoTIFF/DEM) + Arrow (Parquet) IO and run their tests.
geo:
    cmake -S . -B build-geo \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/homebrew \
      {{vcpkg_arg}} -DRFTRACE_ENABLE_GDAL=ON -DRFTRACE_ENABLE_PARQUET=ON
    cmake --build build-geo -j
    ctest --test-dir build-geo --output-on-failure

# Build with the CUDA/OptiX GPU backend and run its suite (needs NVIDIA + OptiX).
# Not part of `ci`; expected to fail to configure on non-NVIDIA hosts. Set
# OptiX_INSTALL_DIR to your OptiX SDK path.
cuda:
    cmake -S . -B build-cuda \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/homebrew \
      {{vcpkg_arg}} -DRFTRACE_ENABLE_CUDA=ON
    cmake --build build-cuda -j
    ctest --test-dir build-cuda --output-on-failure

# --- Python bindings ---------------------------------------------------------
# Interpreter for the Python extension (must have pybind11 + numpy installed).
# Override if needed, e.g. `just py=/usr/bin/python3 py-build`.
py           := "python3"
py_build_dir := "build"
py_path      := justfile_directory() / "bindings/python"

# Configure + build the C++ core and the rftracekit._native extension. The
# pybind11 CMake dir is resolved from the chosen interpreter at build time.
py-build:
    cmake -S . -B {{py_build_dir}} \
      -DCMAKE_PREFIX_PATH="/opt/homebrew;$({{py}} -c 'import pybind11; print(pybind11.get_cmake_dir())')" \
      -DPython3_EXECUTABLE="$({{py}} -c 'import sys; print(sys.executable)')" \
      -DPython_EXECUTABLE="$({{py}} -c 'import sys; print(sys.executable)')" \
      {{vcpkg_arg}} -DRFTRACE_ENABLE_PYTHON=ON
    cmake --build {{py_build_dir}} -j

# Run the Python binding tests (requires py-build first; uses PYTHONPATH).
py-test: py-build
    PYTHONPATH={{py_path}} {{py}} -m pytest {{py_path}} -q

# Validate all OpenSpec specs and changes.
spec:
    openspec validate --all --strict

# Full local CI: clang tests + gcc + asan + spec + examples.
ci: test gcc asan spec examples

# Install the library + headers to a prefix (default ./_install).
install prefix="_install": build
    cmake --install {{build_dir}} --prefix {{prefix}}

# Remove all build directories.
clean:
    rm -rf {{build_dir}} build-debug build-gcc build-asan build-embree build-metal build-opencl build-cuda
