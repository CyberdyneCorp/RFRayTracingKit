# io-python-and-formats

Expose the new geospatial IO (GeoJSON / CityJSON / OSM / terrain / MSI import; CZML / 3D-Tiles /
GeoTIFF / Parquet export) and advanced RF (route/drive-test simulation, narrowband MIMO) to
Python via pybind11, and add two format capabilities at the C++ core: OSM `.osm` XML import
(always-on) plus gated `.osm.pbf` import (libosmium/protozero), and a hierarchical-LOD (quadtree)
3D-Tiles 1.1 exporter. Additive only: the default C++ build and existing Python tests stay green.
