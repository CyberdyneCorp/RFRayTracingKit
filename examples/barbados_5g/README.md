# barbados_5g — 5G sector coverage over real OSM buildings (Bridgetown)

Simulates a 3.5 GHz 5G macro **sector** on a 30 m mast at Station Hill, Bridgetown,
Barbados (13.11221, −59.60354), over a **3 km radius**, using **real OpenStreetMap
building + vegetation footprints** extruded into a 3D scene. Buildings (concrete)
block/shadow line-of-sight; vegetation attenuates.

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

## Notes / honest limitations

- OSM heights are sparse here, so most buildings use a 6 m default — coverage
  shadowing is approximate. Swap in a real DSM/LiDAR height source for accuracy.
- Coverage uses LOS + FSPL (no reflections) for speed over 40k buildings; the 3D
  close-up uses ray-launch with 2 bounces to show multipath. Terrain is flat
  (no DEM); add GeoTIFF terrain (a later phase) for hills.
- The array uses isotropic elements × a cos² element pattern; it approximates a
  panel sector rather than a calibrated antenna file (MSI).
