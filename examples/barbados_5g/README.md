# barbados_5g — 5G sector coverage over real OSM buildings (Bridgetown)

Simulates a 3.5 GHz 5G macro **sector** on a 30 m mast at Station Hill, Bridgetown,
Barbados (13.11221, −59.60354), over a **3 km radius**, using **real OpenStreetMap
building + vegetation footprints** AND an **open DEM** (AWS terrarium tiles, `dem.py`)
so buildings sit on their real ground elevation and the **terrain surface itself
blocks/shadows**. Buildings (concrete) block LOS, terrain relief shadows, vegetation
attenuates, and coverage is evaluated at terrain height (via `CoverageGrid.cell_heights`).

## Run

```bash
just py-build                                                  # build the extension
python3 examples/barbados_5g/fetch_osm.py                      # cache OSM data (Overpass)
PYTHONPATH=bindings/python python3 examples/barbados_5g/barbados_5g.py
```

`fetch_osm.py` downloads buildings + vegetation for the 3 km bbox via the Overpass
API into `osm_bridgetown.json` (~22 MB; git-ignored). Overpass can be busy — the
script retries across mirrors.

Requires a `python3` with `numpy` + `matplotlib`.

## What it does

- Extrudes ~42k OSM building footprints (heights from `height`/`building:levels`,
  else 6 m) into concrete walls + roofs, plus vegetation canopy blocks.
- Models the sector as an 8×8 planar array at 3.5 GHz with a **cos² front-hemisphere
  element pattern** (~30 dB front-to-back), electrically steered to an azimuth +
  downtilt — a directional beamformed sector, not an omni.
- Computes a received-power **coverage map** over the 3 km radius (LOS + FSPL +
  sector gain; vegetation attenuates), then renders:
  - `barbados_coverage.png` — top-down coverage over the city with building
    footprints and the beam azimuth,
  - `barbados_3d.png` — close-up with the extruded buildings, the sector's 3D
    radiation-pattern balloon, and multipath rays to nearby receivers,
  - `barbados_coverage.geojson` — coverage cells for QGIS.

## 6-sector 360° variant

`barbados_6sector.py` reuses the same OSM scene to model a **6-sector 360° site**
(sectors 60° apart) on the 30 m pole at **7.125 GHz** (upper 5G mid-band). It runs
one coverage layer per sector and produces:

- `barbados_6sector_coverage.png` — best-server received power (combined 360° footprint),
- `barbados_6sector_serving.png` — serving-sector map (which of the 6 sectors dominates each cell),
- `barbados_6sector_sinr.png` — inter-sector SINR (kTB+NF noise, 100 MHz / NF 7 dB),
- `barbados_6sector_3d.png` — the six radiation lobes forming the 360° flower on the pole.

```bash
PYTHONPATH=bindings/python python3 examples/barbados_5g/barbados_6sector.py
```

## Notes / honest limitations

- OSM heights are sparse here, so most buildings use a 6 m default — building
  shadowing is approximate. Swap in a real DSM/LiDAR height source for accuracy.
- Terrain comes from the ~30 m SRTM-derived terrarium DEM (interpolated); good for
  hill shadowing, not for fine features. Tiles are cached under `dem_cache/`.
- Coverage uses LOS + FSPL (no reflections) for speed over 40k buildings; the 3D
  close-up uses ray-launch with 2 bounces to show multipath.
- The array uses isotropic elements × a cos² element pattern; it approximates a
  panel sector rather than a calibrated antenna file (MSI).
