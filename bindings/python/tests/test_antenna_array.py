"""Phase 7 antenna-array Python bindings (exposed for the wifi_indoor demo)."""
import math
import warnings

warnings.filterwarnings("ignore")

import numpy as np

import rftracekit as rf
from rftracekit import _native


def test_uniform_planar_array_size():
    lam = 3e8 / 2.4e9
    arr = _native.uniform_planar_array(4, 4, 0.5 * lam, 0.5 * lam, 2.4e9)
    assert arr.size() == 16


def test_steered_gain_peaks_at_beam():
    lam = 3e8 / 2.4e9
    arr = _native.uniform_planar_array(4, 4, 0.5 * lam, 0.5 * lam, 2.4e9,
                                       [1, 0, 0], [0, 1, 0])
    beam = [0, 0, -1]
    g_on = _native.steered_gain_dbi(arr, beam, [0, 0, -1])
    g_off = _native.steered_gain_dbi(arr, beam, [1, 0, 0])
    assert g_on > g_off + 10.0            # strong main lobe vs. side
    assert math.isclose(g_on, 10 * math.log10(16), rel_tol=0.05)  # ~12 dBi


def test_transmitter_array_roundtrips():
    arr = _native.uniform_linear_array(8, 0.06, 2.4e9)
    tx = _native.Transmitter()
    tx.array = arr
    tx.beam_steering = [0, 0, -1]
    assert tx.array is not None and tx.array.size() == 8
    assert list(tx.beam_steering) == [0, 0, -1]


def test_array_gain_enters_link_budget():
    # A steered array on the Tx must change received power vs. the omni default.
    def power(with_array):
        s = rf.Scene()
        tx = _native.Transmitter()
        tx.id = "tx"
        tx.position = [0, 0, 10]
        tx.frequency_hz = 2.4e9
        tx.power_dbm = 20.0
        if with_array:
            lam = 3e8 / 2.4e9
            tx.array = _native.uniform_planar_array(4, 4, 0.5 * lam, 0.5 * lam,
                                                    2.4e9, [1, 0, 0], [0, 1, 0])
            tx.beam_steering = [1, 0, 0]  # steer toward the receiver (+x)
        s.native.add_transmitter(tx)
        s.add_receiver(id="rx", position=[100, 0, 10])
        res = rf.Simulator(rf.SimulationSettings(max_reflections=0)).run(s)
        return res.receiver("rx").received_power_dbm

    gain = power(True) - power(False)
    assert gain > 6.0  # ~10*log10(16) broadside array gain
