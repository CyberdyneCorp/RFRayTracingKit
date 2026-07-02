"""Python route (drive-test) + MIMO bindings (D2 / python-route-mimo)."""
from __future__ import annotations

import json

import numpy as np

import rftracekit as rf
from rftracekit import _native
from conftest import basic_scene, FREQ_HZ


# -- Route --------------------------------------------------------------------
def test_route_roundtrips_construction():
    route = rf.Route(
        waypoints=[[0, 0, 1.5], [100, 0, 1.5]], sample_spacing=25.0, speed_mps=30.0
    )
    assert route.sample_spacing == 25.0
    assert route.speed_mps == 30.0
    wp = np.asarray(route.waypoints)
    assert wp.shape == (2, 3)
    np.testing.assert_allclose(wp[-1], [100, 0, 1.5])


def test_run_route_returns_ordered_samples_with_doppler():
    scene = basic_scene()
    route = rf.Route(
        waypoints=[[0, 0, 1.5], [100, 0, 1.5]], sample_spacing=25.0, speed_mps=30.0
    )
    result = rf.Simulator(
        rf.SimulationSettings(mode="image", max_reflections=0)
    ).run_route(scene, route)

    assert isinstance(result, rf.RouteResult)
    samples = result.samples
    # First waypoint + 25 m steps + terminal endpoint = 5 samples (0..100 m).
    assert len(result) == len(samples) == 5

    # Ordered along the route: monotonically increasing distance and index.
    distances = [s.distance_meters for s in samples]
    assert distances == sorted(distances)
    assert [s.index for s in samples] == list(range(len(samples)))
    assert distances[0] == 0.0
    np.testing.assert_allclose(distances[-1], 100.0)

    # Every sample carries the RF metrics; the clear LOS scene has signal.
    assert all(s.has_signal for s in samples)
    assert all(np.isfinite(s.received_power_dbm) for s in samples)

    # A moving receiver (speed_mps > 0) yields nonzero Doppler on the LOS path
    # for at least the samples that actually move.
    doppler = result.doppler_hz
    assert doppler.shape == (len(samples),)
    assert np.any(np.abs(doppler) > 0.0)


def test_run_route_samples_dataframe_and_exporters(tmp_path):
    scene = basic_scene()
    route = rf.Route(waypoints=[[0, 0, 1.5], [100, 0, 1.5]], sample_spacing=50.0)
    result = rf.Simulator(rf.SimulationSettings()).run_route(scene, route)

    df = result.samples_dataframe()
    assert len(df) == len(result)
    for col in ("index", "distance_m", "received_power_dbm", "doppler_hz"):
        assert col in df.columns

    # JSON / CSV: return-string and write-to-path forms.
    text = result.to_json()
    assert json.loads(text)["route_id"] == "route"
    assert len(json.loads(text)["samples"]) == len(result)

    csv_text = result.to_csv()
    assert csv_text.splitlines()[0].startswith("index,distance_m")

    p = tmp_path / "route.json"
    assert result.to_json(str(p)) is None
    assert json.loads(p.read_text())["frequency_hz"] == FREQ_HZ


# -- MIMO ---------------------------------------------------------------------
def _ula_pair(n_tx=2, n_rx=2):
    lam = 3e8 / FREQ_HZ
    tx = _native.uniform_linear_array(n_tx, 0.5 * lam, FREQ_HZ)
    rx = _native.uniform_linear_array(n_rx, 0.5 * lam, FREQ_HZ)
    return tx, rx


def test_channel_matrix_shape_and_dtype():
    scene = basic_scene()  # already contains receiver "rx" at [100, 0, 1.5]
    result = rf.Simulator(
        rf.SimulationSettings(mode="image", max_reflections=1)
    ).run(scene)

    tx_arr, rx_arr = _ula_pair(n_tx=3, n_rx=2)
    h = rf.mimo.channel_matrix(result.receiver("rx"), tx_arr, rx_arr)
    assert h.shape == (2, 3)  # (n_rx, n_tx)
    assert h.dtype == np.complex128
    assert np.all(np.isfinite(h))


def test_capacity_matches_core_json_and_is_finite():
    scene = basic_scene()  # already contains receiver "rx" at [100, 0, 1.5]
    result = rf.Simulator(rf.SimulationSettings(max_reflections=1)).run(scene)

    tx_arr, rx_arr = _ula_pair()
    h = rf.mimo.channel_matrix(result.receiver("rx"), tx_arr, rx_arr)
    snr = 100.0

    cap = rf.mimo.capacity(h, snr)
    assert np.isfinite(cap) and cap >= 0.0

    # Python capacity must equal the C++ core value embedded in the MIMO JSON.
    core_cap = json.loads(rf.mimo.to_mimo_json(h, snr))["capacity_bps_hz"]
    assert np.isclose(cap, core_cap)
    # Result.to_mimo_json builds the same channel and reports the same capacity.
    res_cap = json.loads(result.to_mimo_json("rx", tx_arr, rx_arr, snr))[
        "capacity_bps_hz"
    ]
    assert np.isclose(cap, core_cap) and np.isclose(res_cap, core_cap)


def test_per_stream_sinr_descending():
    h = np.array([[1.0 + 0j, 0.3 - 0.2j], [0.1 + 0.4j, 0.9 + 0j]], dtype=np.complex128)
    sinr = rf.mimo.per_stream_sinr(h, 50.0)
    assert sinr.shape == (2,)
    assert np.all(sinr >= 0.0)
    assert np.all(np.diff(sinr) <= 1e-12)  # descending


def test_capacity_higher_for_richer_channel():
    # Two channels with identical Frobenius norm: a rank-1 (poor, single spatial
    # stream) vs. a full-rank (rich, two orthogonal streams). At equal SNR and
    # equal energy, the richer channel has strictly higher capacity.
    snr = 100.0
    poor = np.array([[1.0, 1.0], [1.0, 1.0]], dtype=np.complex128)  # rank 1
    rich = np.array([[1.0, 0.0], [0.0, 1.0]], dtype=np.complex128)  # rank 2
    # Normalise both to the same Frobenius norm.
    poor *= np.linalg.norm(rich) / np.linalg.norm(poor)

    cap_poor = rf.mimo.capacity(poor, snr)
    cap_rich = rf.mimo.capacity(rich, snr)
    assert np.isfinite(cap_poor) and np.isfinite(cap_rich)
    assert cap_rich > cap_poor

    # The rich channel supports two nonzero streams; the poor one only a single.
    sinr_rich = rf.mimo.per_stream_sinr(rich, snr)
    sinr_poor = rf.mimo.per_stream_sinr(poor, snr)
    assert np.sum(sinr_rich > 1e-9) == 2
    assert np.sum(sinr_poor > 1e-9) == 1
