## ADDED Requirements

### Requirement: Simulator issues batched backend queries
The simulator SHALL issue its independent-ray backend queries through the caller-owned-output
batched API (`closestHitBatchInto` / `occludedBatchInto`) rather than one `closestHit` / `occluded`
call per ray, for the stages where the rays of a step are mutually independent (do not depend on
each other's hit results). Rays SHALL be gathered into a buffer, dispatched in a single batch, and
their results consumed in input order. The batched path SHALL reuse query buffers across steps
(no per-step output allocation on the hot path).

#### Scenario: Independent-ray stage dispatches as one batch
- **WHEN** a simulation stage evaluates N mutually independent rays (e.g. line-of-sight occlusion
  across many receivers)
- **THEN** the simulator SHALL service them with a single batched backend call, not N single-ray
  calls, and map each result back to its originating query in order

#### Scenario: Dependent-ray stages remain correct
- **WHEN** a stage's rays depend on prior hits (e.g. a ray-launch walk where the next segment
  starts at the previous bounce's hit point)
- **THEN** the simulator SHALL batch only the rays that are independent at each step (e.g. all live
  rays at the same bounce depth) and SHALL NOT reorder dependent work in a way that changes results

### Requirement: Batched path preserves results bit-for-bit
Converting a stage from per-ray to batched queries SHALL NOT change simulation outputs. For any
scene and settings, `Simulator::run`, `runCoverage`, and `runRoute` SHALL produce results identical
to the pre-change per-ray implementation: same paths, same aggregated power / path loss / delay
spread / SINR / Doppler, same coverage-cell values, and the same ordering of paths within a
receiver result. Determinism SHALL be preserved: fixed RNG streams and iteration orders are
unchanged, so repeated runs remain reproducible.

#### Scenario: Batched output equals per-ray output on the CPU backend
- **WHEN** the same scene and settings are simulated on the CPU backend
- **THEN** every receiver/cell/route result SHALL equal the result the per-ray implementation
  produced (asserted by the existing golden/regression suites remaining green, and by a
  batched-vs-reference equality test)

#### Scenario: Repeated batched runs match
- **WHEN** a batched simulation is run twice with identical inputs
- **THEN** the two results SHALL be identical, element by element

### Requirement: Batched path is CPU-neutral and backend-agnostic
The batched simulator path SHALL NOT require any backend change and SHALL leave the CPU backend's
performance and results unaffected (the caller-owned batched primitive already loops the single-ray
query for backends that do not override it). GPU backends (CUDA, Metal, OpenCL) SHALL accelerate a
full run through the same code path by virtue of their single-dispatch overrides. RF physics, the
scene model, and result formats SHALL be unchanged.

#### Scenario: GPU coverage run matches CPU
- **WHEN** a coverage grid is simulated on the CUDA backend and on the CPU backend for the same
  scene and settings, on a host with a CUDA device
- **THEN** the two coverage results SHALL agree within the established CPU-vs-GPU parity tolerance
  (float-vs-double), and the GPU run SHALL issue batched dispatches rather than per-ray queries

#### Scenario: CPU results unchanged when a stage is batched
- **WHEN** a stage is migrated from per-ray to batched queries and run on the CPU backend
- **THEN** its results SHALL be identical to before the migration, with no new allocation on the
  per-ray-equivalent path beyond the reused batch buffers
