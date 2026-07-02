#pragma once

#include <string>

#include "rftrace/antenna.hpp"

namespace rftrace::io {

/// Load a Planet/MSI antenna pattern (`.msi`/`.pln` text) into an
/// `AntennaPattern`.
///
/// The format is a keyword/value text file:
///   * `NAME <text>`                 — pattern name (ignored by the model)
///   * `FREQUENCY <MHz>`             — design frequency (ignored by the model)
///   * `GAIN <value> [dBd|dBi]`      — peak gain; `dBd` is normalized to dBi by
///                                     adding 2.15 dB. A missing unit is dBi.
///   * `HORIZONTAL <n>` + n rows     — `angle attenuation` (deg, dB down from
///                                     peak) → `azimuthCutDb` (stored as −atten)
///   * `VERTICAL <n>`  + n rows      — same, → `verticalCutDb`
/// Other keywords (TILT, POLARIZATION, COMMENT, …) and blank lines are ignored.
///
/// The returned pattern is directional (`omni=false`) with boresight +X and
/// up +Z. Throws `SceneError` when the file is missing, lacks a `GAIN` line, or
/// contains no `HORIZONTAL`/`VERTICAL` table (rather than a partial pattern).
AntennaPattern loadMsiAntenna(const std::string& path);

}  // namespace rftrace::io
