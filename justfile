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
    rm -rf {{build_dir}} build-debug build-gcc build-asan build-embree
