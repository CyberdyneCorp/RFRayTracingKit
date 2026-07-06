# Contributing to RFTraceKit

Thanks for your interest in RFTraceKit — a modern C++20 ray tracing and RF propagation
library. This guide covers how to build, the workflow we follow, and what a mergeable change
looks like.

## Getting set up

```bash
git clone https://github.com/CyberdyneCorp/RFRayTracingKit.git
cd RFRayTracingKit
just build          # configure + compile the library, tests, and examples
just test           # build + run the full CTest suite (CPU backend)
```

No `just`? The equivalent is a plain CMake invocation:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Dependencies (Eigen, Assimp, nlohmann/json) resolve via **vcpkg** (set `VCPKG_ROOT`) or any
system package manager through CMake config-mode `find_package`. GoogleTest is fetched
automatically at a pinned version. GPU work: run `just gpu-detect` first, then
`just cuda` / `just opencl` / `just metal` / `just embree` for the matching backend.

## Development workflow

We develop spec-first with [OpenSpec](https://openspec.dev). Living capability specs are in
[`openspec/specs/`](openspec/specs); changes are proposed, designed, and validated under
[`openspec/changes/`](openspec/changes) before implementation.

- **Medium or large features** (new RF physics, ray backends, importers/exporters, bindings,
  build/packaging behavior): start with an OpenSpec change (proposal → specs/tasks) before
  implementing. Run `openspec validate --all --strict` — CI enforces it.
- **Small fixes and docs:** a direct PR is fine. Still keep the specs and docs truthful — if
  your change makes a spec or doc statement false, update it in the same PR.

## What a mergeable PR looks like

- **Tests.** New behavior ships with tests. **Every bug fix includes a regression test** that
  fails before the fix and passes after.
- **Green CI.** The default C++ core build, the CLI tools, the C API, and OpenSpec validation
  must all pass on Linux/clang.
- **Docs/specs in sync.** Update `README.md` and `openspec/specs/` when your change affects
  documented behavior. Don't leave a claim the code contradicts.
- **Readable, low-complexity code.** Match the surrounding style (modern C++20, Eigen types,
  smart pointers, contiguous data — no raw owning pointers, no legacy macros). Keep
  per-function cognitive complexity modest; isolate genuinely irreducible algorithms (BVH
  traversal, ray-launch kernels) and flag them rather than mangling them to hit a number.
- **A descriptive PR message.** Explain what changed and why. If you found a bug, describe how
  it reproduced.

## Commit & PR style

- Concise, technical, imperative commit subjects (e.g. "Fix UTD diffraction coefficient sign").
- Reference the OpenSpec change or issue when there is one.
- Keep unrelated changes in separate PRs.

## Numerical correctness

The pure-C++ CPU BVH backend (fp64) is the **reference oracle**. Every accelerated backend
(Embree, CUDA/OptiX, Metal, OpenCL) is validated against it within a documented fp32 tolerance
(the parity suites, e.g. `test_embree_parity.cpp`). If you change a propagation model, material
interaction, diffraction path, or backend kernel, make sure the relevant CPU-vs-backend parity
tests still pass — and add one if a gap exists.

## Reporting bugs & requesting features

Open an issue using the templates. For **security** issues, do **not** open a public issue —
see [SECURITY.md](SECURITY.md).

## License

By contributing, you agree that your contributions are licensed under the project's
[MIT License](LICENSE).
