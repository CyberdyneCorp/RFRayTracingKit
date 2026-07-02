"""Open DEM (AWS Terrarium tiles) sampler + terrain mesh for the examples.

Downloads terrarium elevation tiles (no API key) covering a region, decodes them
to metres, and provides bilinear elevation lookup in lat/lon or local ENU metres,
plus a triangulated terrain surface for the scene.
"""
import io
import math
import os
import urllib.request

import numpy as np

try:
    from PIL import Image
    _HAVE_PIL = True
except Exception:  # pragma: no cover
    import matplotlib.image as _mpimg
    _HAVE_PIL = False

_TILE = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"


class DEM:
    """Elevation mosaic over a lat/lon bbox from terrarium tiles."""

    def __init__(self, lat0, lon0, half_m, zoom=14, cache_dir=None):
        self.lat0, self.lon0, self.z = lat0, lon0, zoom
        self.mx = 111320.0 * math.cos(math.radians(lat0))
        self.my = 110540.0
        dlat = half_m / self.my
        dlon = half_m / self.mx
        self.x0 = int(self._xf(lon0 - dlon))
        x1 = int(self._xf(lon0 + dlon))
        self.y0 = int(self._yf(lat0 + dlat))  # north = smaller tile y
        y1 = int(self._yf(lat0 - dlat))
        rows, cols = (y1 - self.y0 + 1), (x1 - self.x0 + 1)
        self.mos = np.zeros((rows * 256, cols * 256))
        for ty in range(self.y0, y1 + 1):
            for tx in range(self.x0, x1 + 1):
                el = self._tile(tx, ty, cache_dir)
                self.mos[(ty - self.y0) * 256:(ty - self.y0 + 1) * 256,
                         (tx - self.x0) * 256:(tx - self.x0 + 1) * 256] = el

    def _xf(self, lon):
        return (lon + 180.0) / 360.0 * (2 ** self.z)

    def _yf(self, lat):
        r = math.radians(lat)
        return (1 - math.log(math.tan(r) + 1 / math.cos(r)) / math.pi) / 2 * (2 ** self.z)

    def _tile(self, tx, ty, cache_dir):
        path = None
        if cache_dir:
            os.makedirs(cache_dir, exist_ok=True)
            path = os.path.join(cache_dir, f"{self.z}_{tx}_{ty}.png")
        if path and os.path.exists(path):
            data = open(path, "rb").read()
        else:
            data = urllib.request.urlopen(
                _TILE.format(z=self.z, x=tx, y=ty), timeout=60).read()
            if path:
                open(path, "wb").write(data)
        if _HAVE_PIL:
            im = np.asarray(Image.open(io.BytesIO(data)).convert("RGB")).astype(float)
        else:
            im = _mpimg.imread(io.BytesIO(data))[:, :, :3] * 255.0
        return (im[:, :, 0] * 256 + im[:, :, 1] + im[:, :, 2] / 256) - 32768

    def elev_latlon(self, lat, lon):
        lat = np.asarray(lat, float)
        lon = np.asarray(lon, float)
        col = ((lon + 180.0) / 360.0 * (2 ** self.z) - self.x0) * 256
        r = np.radians(lat)
        yf = (1 - np.log(np.tan(r) + 1 / np.cos(r)) / math.pi) / 2 * (2 ** self.z)
        row = (yf - self.y0) * 256
        return self._bilinear(row, col)

    def elev_enu(self, x, y):
        """Elevation (m) at local ENU offset (east x, north y) metres."""
        lat = self.lat0 + np.asarray(y, float) / self.my
        lon = self.lon0 + np.asarray(x, float) / self.mx
        return self.elev_latlon(lat, lon)

    def _bilinear(self, row, col):
        h, w = self.mos.shape
        row = np.clip(row, 0, h - 1.001)
        col = np.clip(col, 0, w - 1.001)
        r0 = np.floor(row).astype(int)
        c0 = np.floor(col).astype(int)
        fr, fc = row - r0, col - c0
        m = self.mos
        return (m[r0, c0] * (1 - fr) * (1 - fc) + m[r0 + 1, c0] * fr * (1 - fc) +
                m[r0, c0 + 1] * (1 - fr) * fc + m[r0 + 1, c0 + 1] * fr * fc)


def terrain_triangles(dem, half_m, step_m, triangle_ctor):
    """A triangulated terrain surface over [-half,half]^2 (ENU) at `step_m`."""
    xs = np.arange(-half_m, half_m + step_m, step_m)
    X, Y = np.meshgrid(xs, xs)
    Z = dem.elev_enu(X.ravel(), Y.ravel()).reshape(X.shape)
    tris = []
    for i in range(len(xs) - 1):
        for j in range(len(xs) - 1):
            a = [xs[j], xs[i], Z[i, j]]
            b = [xs[j + 1], xs[i], Z[i, j + 1]]
            c = [xs[j + 1], xs[i + 1], Z[i + 1, j + 1]]
            d = [xs[j], xs[i + 1], Z[i + 1, j]]
            tris.append(triangle_ctor(a, b, c))
            tris.append(triangle_ctor(a, c, d))
    return tris
