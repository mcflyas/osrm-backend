#!/usr/bin/env python3
"""Create a seeded, road-snapped synthetic Oslo/Akershus table workload."""

import argparse
import hashlib
import http.client
import json
import math
import random
import re
import time
from collections import Counter, defaultdict
from pathlib import Path
from urllib.parse import urlencode, urlsplit

# Approximate sampling areas, not administrative boundaries or observed demand.
REGIONS = [
    ("oslo", 1176, (10.62, 59.85, 10.94, 60.02)),
    ("west_asker_baerum", 672, (10.36, 59.78, 10.62, 59.97)),
    ("northeast_lillestrom_lorenskog", 672, (10.94, 59.88, 11.20, 60.04)),
    ("south_follo", 504, (10.73, 59.62, 10.99, 59.84)),
    ("north_jessheim_eidsvoll", 336, (11.00, 60.05, 11.27, 60.35)),
]
SIZES = (500, 1000, 2000, 3360)
HUB = (10.75261, 59.913953)


def distance_metres(first, second):
    lat1, lat2 = math.radians(first[1]), math.radians(second[1])
    delta_lon = math.radians(second[0] - first[0])
    value = math.sin((lat2 - lat1) / 2) ** 2
    value += math.cos(lat1) * math.cos(lat2) * math.sin(delta_lon / 2) ** 2
    return 2 * 6371000 * math.asin(min(1, math.sqrt(value)))


class SpacedPoints:
    def __init__(self, minimum_distance):
        self.minimum_distance = minimum_distance
        self.cells = defaultdict(list)

    def add(self, coordinate):
        # Conservative metre scales for the sampling boxes at 59–61 degrees north.
        x = math.floor(coordinate[0] * 50000 / self.minimum_distance)
        y = math.floor(coordinate[1] * 100000 / self.minimum_distance)
        for i in range(x - 1, x + 2):
            for j in range(y - 1, y + 2):
                if any(
                    distance_metres(coordinate, other) < self.minimum_distance
                    for other in self.cells.get((i, j), ())
                ):
                    return False
        self.cells[x, y].append(coordinate)
        return True


class OsrmClient:
    def __init__(self, base_url, profile):
        endpoint = urlsplit(base_url)
        if endpoint.scheme not in {"http", "https"} or not endpoint.hostname:
            raise ValueError("base URL must be HTTP or HTTPS")
        if endpoint.query or endpoint.fragment or endpoint.username or endpoint.password:
            raise ValueError("base URL must not contain credentials, query or fragment")
        if not re.fullmatch(r"[A-Za-z0-9_-]+", profile):
            raise ValueError("invalid profile name")
        connection = (
            http.client.HTTPSConnection if endpoint.scheme == "https" else http.client.HTTPConnection
        )
        self.connection = connection(endpoint.hostname, endpoint.port, timeout=180)
        self.prefix = endpoint.path.rstrip("/")
        self.profile = profile

    def request(self, service, coordinates, **options):
        locations = ";".join(f"{lon:.6f},{lat:.6f}" for lon, lat in coordinates)
        path = f"{self.prefix}/{service}/v1/{self.profile}/{locations}"
        if options:
            path += "?" + urlencode(options, safe=",;")
        for attempt in range(2):
            try:
                self.connection.request("GET", path)
                response = self.connection.getresponse()
                body = response.read()
                break
            except (http.client.RemoteDisconnected, ConnectionResetError, BrokenPipeError):
                self.connection.close()
                if attempt:
                    raise
        result = json.loads(body)
        if response.status != 200 or result.get("code") != "Ok":
            raise RuntimeError(f"{service} HTTP {response.status}: {result}")
        return result, len(body)

    def close(self):
        self.connection.close()


def generate(client, seed, max_snap_distance, minimum_spacing):
    rng = random.Random(seed)
    points = []
    used = SpacedPoints(minimum_spacing)
    used.add(HUB)
    attempts = Counter()
    disconnected = Counter()
    for _ in range(10):
        counts = Counter(point["region"] for point in points)
        for name, target, bounds in REGIONS:
            west, south, east, north = bounds
            while counts[name] < target:
                attempts[name] += 1
                if attempts[name] > target * 100:
                    raise RuntimeError(f"Cannot find enough suitable road points in {name}")
                candidate = [rng.uniform(west, east), rng.uniform(south, north)]
                result, _ = client.request("nearest", [candidate], number=1)
                waypoint = result["waypoints"][0]
                coordinate = tuple(round(value, 6) for value in waypoint["location"])
                if (
                    len(coordinate) != 2
                    or not all(math.isfinite(value) for value in coordinate)
                    or not math.isfinite(waypoint["distance"])
                    or waypoint["distance"] > max_snap_distance
                    or not west <= coordinate[0] <= east
                    or not south <= coordinate[1] <= north
                    or not used.add(coordinate)
                ):
                    continue
                points.append({"coordinate": coordinate, "region": name})
                counts[name] += 1
            print(f"Sampled {counts[name]} points in {name}", flush=True)

        coordinates = [HUB, *(point["coordinate"] for point in points)]
        outward, _ = client.request(
            "table", coordinates, sources="0", annotations="duration", skip_waypoints="true"
        )
        inward, _ = client.request(
            "table", coordinates, destinations="0", annotations="duration", skip_waypoints="true"
        )
        retained = []
        for index, point in enumerate(points, 1):
            if outward["durations"][0][index] is None or inward["durations"][index][0] is None:
                disconnected[point["region"]] += 1
            else:
                retained.append(point)
        if len(retained) == len(points):
            rng.shuffle(points)
            return points, attempts, disconnected
        points = retained
    raise RuntimeError("Could not select a mutually reachable set of points")


def validate_table(result, size):
    for field in ("durations", "distances"):
        rows = result[field]
        if len(rows) != size or any(len(row) != size for row in rows):
            raise ValueError(f"Unexpected {field} dimensions for {size} locations")
        for source, row in enumerate(rows):
            for destination, value in enumerate(row):
                if value is None or not math.isfinite(value) or value < 0:
                    raise ValueError(f"Invalid {field} at {source}, {destination}: {value}")
        if any(rows[index][index] != 0 for index in range(size)):
            raise ValueError(f"{field} diagonal must be zero")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default="http://127.0.0.1:5010")
    parser.add_argument("--profile", default="driving")
    parser.add_argument("--seed", type=int, default=20261027)
    parser.add_argument("--max-snap-distance", type=float, default=150)
    parser.add_argument("--minimum-spacing", type=float, default=10)
    parser.add_argument("--source-description", default="User-supplied OSRM graph")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--validate-tables", action="store_true")
    args = parser.parse_args()
    if not math.isfinite(args.max_snap_distance) or args.max_snap_distance <= 0:
        parser.error("max-snap-distance must be positive and finite")
    if not math.isfinite(args.minimum_spacing) or args.minimum_spacing <= 0:
        parser.error("minimum-spacing must be positive and finite")
    if args.output.exists():
        parser.error("output directory must not exist")
    client = OsrmClient(args.base_url, args.profile)
    try:
        points, attempts, disconnected = generate(
            client, args.seed, args.max_snap_distance, args.minimum_spacing
        )
        manifest = {
            "version": 1,
            "synthetic": True,
            "seed": args.seed,
            "coordinate_order": "longitude,latitude",
            "max_snap_distance_metres": args.max_snap_distance,
            "minimum_spacing_metres": args.minimum_spacing,
            "source": {
                "base_url": args.base_url,
                "profile": args.profile,
                "description": args.source_description,
            },
            "sampling": "uniform candidates per box; nearest-road snap; minimum spacing; seeded shuffle",
            "regions": [
                {
                    "name": name,
                    "count": count,
                    "bbox": bounds,
                    "attempts": attempts[name],
                    "disconnected_rejections": disconnected[name],
                }
                for name, count, bounds in REGIONS
            ],
            "reachability_hub": HUB,
            "reachable_to_and_from_hub": True,
            "cases": [],
        }
        artifacts = {}
        for size in SIZES:
            selected = points[:size]
            request = {
                "coordinates": [point["coordinate"] for point in selected],
                "annotations": ["duration", "distance"],
                "skip_waypoints": True,
            }
            content = (json.dumps(request, separators=(",", ":"), allow_nan=False) + "\n").encode()
            filename = f"table-{size}.json"
            artifacts[filename] = content
            case = {
                "file": filename,
                "locations": size,
                "cells_per_annotation": size * size,
                "sha256": hashlib.sha256(content).hexdigest(),
                "region_counts": dict(sorted(Counter(p["region"] for p in selected).items())),
            }
            if args.validate_tables:
                started = time.perf_counter()
                result, response_bytes = client.request(
                    "table",
                    request["coordinates"],
                    annotations="duration,distance",
                    skip_waypoints="true",
                )
                elapsed = time.perf_counter() - started
                validate_table(result, size)
                del result
                case["validation"] = {
                    "status": "passed",
                    "null_cells": 0,
                    "response_bytes": response_bytes,
                    "request_and_json_decode_seconds": round(elapsed, 3),
                }
                print(f"Validated {size} x {size}: {elapsed:.3f}s, {response_bytes} bytes", flush=True)
            manifest["cases"].append(case)
        args.output.mkdir(parents=True)
        for filename, content in artifacts.items():
            (args.output / filename).write_bytes(content)
        (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"Wrote workload to {args.output}", flush=True)
    finally:
        client.close()


if __name__ == "__main__":
    main()
