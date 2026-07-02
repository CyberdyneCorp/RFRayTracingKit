#pragma once

#include <limits>
#include <string>
#include <vector>

#include "rftrace/math.hpp"

namespace rftrace {

/// A regular horizontal coverage grid evaluated at a fixed height. `origin` is
/// the (x, y) of the grid's lower corner; cells are `cellSize` metres apart.
struct CoverageGrid {
  Vec3 origin{0.0, 0.0, 0.0};
  double cellSize = 2.0;
  int cols = 1;
  int rows = 1;
  double height = 1.5;
  /// Optional per-cell evaluation height (row-major, size rows*cols). When set,
  /// it overrides the flat `height` — e.g. to follow terrain (ground + rx height).
  std::vector<double> cellHeights;

  int cellCount() const { return cols * rows; }

  /// World-space centre of cell (row, col). Uses the per-cell height when
  /// `cellHeights` is populated, otherwise the flat grid `height`.
  Vec3 cellCenter(int row, int col) const {
    const double z = cellHeights.empty() ? height
                                         : cellHeights[row * cols + col];
    return Vec3{origin.x() + (col + 0.5) * cellSize,
                origin.y() + (row + 0.5) * cellSize, z};
  }
};

/// Result of a coverage-grid simulation: a row-major 2D array of received power
/// (and path loss) plus the grid that produced it.
struct CoverageResult {
  static constexpr double NoSignal = -std::numeric_limits<double>::infinity();

  CoverageGrid grid;
  std::string simulationId;
  double frequencyHz = 0.0;
  std::vector<double> powerDbm;    ///< row-major, size rows*cols
  std::vector<double> pathLossDb;  ///< row-major, size rows*cols

  /// SINR (dB) per cell, row-major. Populated only when SINR is enabled;
  /// otherwise left empty. Unreached cells carry the `NoSignal` sentinel.
  std::vector<double> sinrDb;

  double powerAt(int row, int col) const {
    return powerDbm[row * grid.cols + col];
  }

  double sinrAt(int row, int col) const {
    return sinrDb[row * grid.cols + col];
  }
};

}  // namespace rftrace
