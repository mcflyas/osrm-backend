# Synthetic Oslo and Akershus matrix workload

This workload contains 3,360 distinct synthetic road locations, with nested
500-, 1,000- and 2,000-location subsets. It matches the matrix sizes discussed
for the Skoleskyss workload. The geographic distribution is an explicit sampling
assumption; no passenger, school-trip or address records were used.

| Request file | Locations | Cells in each returned table |
| --- | ---: | ---: |
| [table-500.json](table-500.json) | 500 | 250,000 |
| [table-1000.json](table-1000.json) | 1,000 | 1,000,000 |
| [table-2000.json](table-2000.json) | 2,000 | 4,000,000 |
| [table-3360.json](table-3360.json) | 3,360 | 11,289,600 |

Every request asks for both durations and distances, omits waypoint output, and
uses all supplied locations as both sources and destinations. The first 500
coordinates of the full request are exactly the 500-location request, and the
same applies to the other subsets. Coordinates use longitude, latitude order.

## Sampling

| Approximate sampling area | Points | Share |
| --- | ---: | ---: |
| Oslo | 1,176 | 35% |
| West: Asker and Bærum | 672 | 20% |
| Northeast: Lillestrøm and Lørenskog | 672 | 20% |
| South: Follo | 504 | 15% |
| North: Jessheim and Eidsvoll | 336 | 10% |

The generator uses seed `20261027` and the bounding boxes recorded in
[manifest.json](manifest.json). These boxes approximate travel areas, rather
than following municipality boundaries. It draws uniform candidates within
each box, snaps them to the nearest car-road location, and rejects snaps more
than 150 metres away or outside the box. Road locations must be at least 10
metres apart using spherical great-circle distance. A seeded shuffle mixes the
areas before taking the nested subsets.

The spacing check avoids nearly coincident snapped locations. During initial
validation, the existing engine returned a distance of −0.1 metres between two
distinct coordinates only about 6 cm apart. That pair was excluded by the
spacing rule; no matrix values were edited or clamped.

## Validation and provenance

Generation and validation used the existing local Norway CH server,
`http://127.0.0.1:5010`, profile label `driving`, OSRM v26.8.0. The manifest records
the container image identity, graph filename, sampling attempts, per-subset
area counts and SHA-256 hashes of the request files.

Every point is reachable to and from a common Oslo road location. All four full
tables were then checked for exact dimensions, zero diagonals, finite
non-negative values and **zero unreachable cells**, for both annotations.
This validation applies to the driving graph; additional profiles need their
own validation.

The manifest's elapsed times are single validation runs through the existing
Docker server and include HTTP transfer and Python JSON decoding. They are not
a measurement of multi-profile overhead or a direct comparison with the
October 7 native benchmark. The largest response was approximately 162 MB.

Road coordinates are derived from OpenStreetMap data served by the local OSRM
graph. Map data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright),
available under the Open Database License.

## Replay with this branch

Run these commands from the repository root. The deployment must contain a
`car` profile prepared for Norway or Østlandet. Use a table limit of at least
3,360; `4000` leaves some room:

```sh
build/osrm-routed --config /path/to/deployment.json \
  --max-table-size 4000 --ip 127.0.0.1 --port 5500
```

The request files are JSON bodies for this branch's POST table API:

```sh
curl --fail --silent --show-error \
  -H 'Content-Type: application/json' \
  --data-binary @example/multiple-profiles/oslo-akershus/table-3360.json \
  --output /dev/null \
  --write-out 'HTTP %{http_code}; %{time_total}s; %{size_download} bytes\n' \
  http://127.0.0.1:5500/table/v1/car
```

Change the file to select a smaller case and the URL profile to select another
configured engine. Keep the same saved files, graph, annotations and total
worker count when comparing standalone and combined deployments. The existing
`benchmark_multiple_profiles.py` driver still benchmarks route requests; these
files supply the inputs for a separate table comparison.

## Regenerate

The generator uses only Python's standard library and an existing OSRM HTTP
server. It uses GET requests, including for validation, so it also works with
older servers that lack POST support. The server must allow at least 3,361
coordinates for the hub validation and the full 3,360 × 3,360 table; a table
limit of `4000` is suitable. A fresh output directory is required.

```sh
python3 scripts/generate_oslo_matrix_workload.py \
  --base-url http://127.0.0.1:5010 --profile driving \
  --seed 20261027 --minimum-spacing 10 --max-snap-distance 150 \
  --source-description 'Describe the graph and OSRM version used here' \
  --output build/oslo-akershus-regenerated --validate-tables
```

The same seed and graph reproduce the request files. Changing the graph can
change snapping and therefore the sampled coordinates. Validation timings in
the manifest vary between runs. The generator writes the workload only after
generation and the requested table validation succeed.
