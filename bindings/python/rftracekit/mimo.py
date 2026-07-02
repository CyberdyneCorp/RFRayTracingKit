"""Narrowband MIMO channel matrix, capacity, and per-stream SINR (D2 / R4).

Thin, numpy-friendly wrappers over ``rftracekit._native.mimo``. The channel
matrix ``H`` is a NumPy complex128 array of shape ``(n_rx, n_tx)``; capacity and
per-stream SINR accept such an ``H`` and a linear SNR.
"""
from __future__ import annotations

import numpy as np

from . import _native
from ._util import native_of


def channel_matrix(receiver_result, tx_array, rx_array) -> np.ndarray:
    """Assemble the MIMO channel matrix ``H`` (``complex128[n_rx, n_tx]``).

    ``receiver_result`` is a :class:`ReceiverResult` (or wrapper); ``tx_array`` /
    ``rx_array`` are :class:`AntennaArray` geometries. Each propagation path
    contributes a rank-1 term, so richer multipath raises the channel rank.
    """
    h = _native.mimo.channel_matrix(
        native_of(receiver_result), native_of(tx_array), native_of(rx_array)
    )
    return np.asarray(h, dtype=np.complex128)


def capacity(channel, snr_linear: float) -> float:
    """Equal-power narrowband capacity (bits/s/Hz) at a given linear SNR."""
    h = np.asarray(channel, dtype=np.complex128)
    return _native.mimo.capacity(h, float(snr_linear))


def per_stream_sinr(channel, snr_linear: float) -> np.ndarray:
    """Per-stream SINRs (descending) for the eigenmodes of ``H·Hᴴ``."""
    h = np.asarray(channel, dtype=np.complex128)
    return np.asarray(
        _native.mimo.per_stream_sinr(h, float(snr_linear)), dtype=np.float64
    )


def to_mimo_json(channel, snr_linear: float, path=None):
    """Serialize a channel matrix + capacity to JSON; write ``path`` or return it."""
    h = np.asarray(channel, dtype=np.complex128)
    if path is None:
        return _native.io.mimo_to_json_string(h, float(snr_linear))
    _native.io.export_mimo_json(h, float(snr_linear), str(path))
    return None


__all__ = ["channel_matrix", "capacity", "per_stream_sinr", "to_mimo_json"]
