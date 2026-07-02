#!/usr/bin/env python3
"""Fetch OSM buildings + vegetation around a point via Overpass, cache to JSON."""
import json, math, os, sys, time, urllib.request

LAT, LON, R = 13.11220679386025, -59.603538511368605, 3000.0
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "osm_bridgetown.json")

dlat = R / 110540.0
dlon = R / (111320.0 * math.cos(math.radians(LAT)))
s, w, n, e = LAT - dlat, LON - dlon, LAT + dlat, LON + dlon
bbox = f"{s},{w},{n},{e}"

q = f"""[out:json][timeout:300];
(
  way["building"]({bbox});
  way["natural"~"wood|scrub|tree_row"]({bbox});
  way["landuse"~"forest|grass|meadow"]({bbox});
  way["leisure"="park"]({bbox});
);
out body; >; out skel qt;"""

ENDPOINTS = [
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass-api.de/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
]
print(f"bbox={bbox}", file=sys.stderr)
for url in ENDPOINTS:
    try:
        print(f"trying {url} ...", file=sys.stderr)
        req = urllib.request.Request(url, data=q.encode(),
                                     headers={"User-Agent": "RFTraceKit-demo"})
        with urllib.request.urlopen(req, timeout=300) as r:
            data = json.load(r)
        break
    except Exception as ex:
        print(f"  failed: {ex}", file=sys.stderr)
        time.sleep(2)
else:
    print("ALL_ENDPOINTS_FAILED"); sys.exit(3)

els = data.get("elements", [])
ways = [x for x in els if x["type"] == "way"]
blds = [x for x in ways if "building" in x.get("tags", {})]
veg = [x for x in ways if "building" not in x.get("tags", {})]
with open(OUT, "w") as f:
    json.dump(data, f)
print(f"elements={len(els)} ways={len(ways)} buildings={len(blds)} veg={len(veg)} "
      f"bytes={os.path.getsize(OUT)}")
