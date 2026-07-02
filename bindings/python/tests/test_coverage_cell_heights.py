"""CoverageGrid.cell_heights: per-cell evaluation height (terrain support)."""
import math
import warnings

warnings.filterwarnings("ignore")

import rftracekit as rf
from rftracekit import _native


def _scene():
    # A wall from z=0..8 at x=50 (spanning y).
    s = rf.Scene()
    s.add_mesh([_native.Triangle([50, -60, 0], [50, 60, 0], [50, 60, 8]),
                _native.Triangle([50, -60, 0], [50, 60, 8], [50, -60, 8])], "")
    tx = _native.Transmitter()
    tx.id = "t"
    tx.position = [0, 0, 6]
    tx.frequency_hz = 3.5e9
    tx.power_dbm = 30.0
    s.native.add_transmitter(tx)
    return s


def _grid():
    g = _native.CoverageGrid()
    g.origin = [100, -10, 0]   # single cell centred at x=110 (behind the wall)
    g.cell_size = 20
    g.cols = 1
    g.rows = 1
    return g


def test_cell_heights_override_flat_height():
    s = _scene()
    sim = rf.Simulator(rf.SimulationSettings(max_reflections=0))

    g = _grid()
    g.height = 1.5                       # low: LOS crosses the wall -> blocked
    low = sim.run_coverage(s, g)
    assert not math.isfinite(float(low.power_dbm[0]))

    g.cell_heights = [20.0]              # raised above the wall top -> clear LOS
    high = sim.run_coverage(s, g)
    assert math.isfinite(float(high.power_dbm[0]))
