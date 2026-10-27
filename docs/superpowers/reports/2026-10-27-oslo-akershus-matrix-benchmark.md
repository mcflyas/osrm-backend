# Oslo and Akershus matrix benchmark

On 27 October 2026, one native OSRM process serving car and foot CH graphs had
essentially the same median car-table latency as separate processes at the
tested load. With only car requests, the 3,360 × 3,360 matrix took 2.783 seconds
combined versus 2.781 seconds standalone. With concurrent foot traffic, it took
2.880 versus 2.888 seconds. These are warm HTTP measurements on an Apple M4 Max
using synthetic locations and two server workers per deployment.

## Car traffic with both profiles loaded

Each value is the median of five measured requests after one warmup per size
and deployment. Both servers had two workers. The combined server loaded both
profiles, but only car received requests in this phase. Positive differences
mean the combined server was slower.

| Matrix | Standalone seconds | Combined seconds | Difference |
| --- | ---: | ---: | ---: |
| 500 × 500 | 0.137 | 0.140 | +2.08% |
| 1,000 × 1,000 | 0.379 | 0.377 | −0.52% |
| 2,000 × 2,000 | 1.172 | 1.161 | −0.97% |
| 3,360 × 3,360 | 2.781 | 2.783 | +0.08% |

The largest request ranged from 2.770 to 2.897 seconds standalone and 2.765 to
2.792 seconds combined. These samples show no consistent latency penalty from
loading and dispatching multiple profiles.

## Alternating profiles

A 500 × 500 foot table immediately preceded each measured car table. The
standalone deployment used one worker per process; the combined process used
two workers. Requests were sequential, with five measured pairs per size and
deployment. Worker scheduling was unconstrained, so this phase does not force
every request to switch the dataset on the same worker.

| Car matrix | Standalone seconds | Combined seconds | Difference |
| --- | ---: | ---: | ---: |
| 500 × 500 | 0.132 | 0.132 | −0.09% |
| 1,000 × 1,000 | 0.365 | 0.364 | −0.21% |
| 2,000 × 2,000 | 1.119 | 1.127 | +0.71% |
| 3,360 × 3,360 | 2.842 | 2.824 | −0.65% |

## Concurrent car and foot traffic

One client issued two consecutive 3,360 × 3,360 car tables while another issued
500 × 500 foot tables continuously, waiting for each response before sending
the next. Three batches per deployment produced six large requests and 59
standalone or 58 combined foot requests. There were at most two requests in
flight. Worker allocation matched the alternating phase.

| Measurement | Separate processes | Combined process |
| --- | ---: | ---: |
| Car median seconds | 2.888 | 2.880 |
| Car observed range seconds | 2.859–2.915 | 2.864–2.930 |
| Foot median seconds | 0.304 | 0.303 |
| Foot requests per second | 3.298 | 3.293 |
| Peak sampled server RSS GiB | 3.222 | 3.235 |

The combined car median was 0.27% lower, while foot throughput was 0.16% lower.
These small differences do not indicate a meaningful performance advantage for
either deployment. Foot throughput includes the final in-flight foot request
after the car client finishes, and measures this closed-loop workload rather
than maximum server capacity.

## Memory

RSS was sampled with `ps` throughout each phase, including warmups. Standalone
RSS is the sum across its active server processes. Values exclude the client,
preprocessing and the user's existing services.

| Phase | Standalone peak GiB | Combined peak GiB |
| --- | ---: | ---: |
| Only car receives requests | 2.930 | 2.256 |
| Alternating car and foot | 3.161 | 3.192 |
| Concurrent car and foot | 3.222 | 3.235 |

With both profiles active, measured server RSS was similar. The lower combined
RSS in the car-only phase is not evidence of graph deduplication: mmap residency,
worker scheduling and allocator retention affect RSS, and the foot graph was
not exercised in that phase. Summed process RSS can count shared pages more
than once. This sampling does not establish total physical memory requirements
or long-term peaks. The prepared artifact directories occupied about 1.8 GiB
for car and 3.3 GiB for foot, including preparation artifacts.

## Workload and verification

The [saved workload](../../../example/multiple-profiles/oslo-akershus/README.md)
contains 3,360 unique road locations distributed across Oslo and Akershus,
with nested smaller subsets. Each request returns both durations and distances
and skips waypoint metadata. It matches the requested matrix dimensions, with
an assumed geographic distribution rather than a measured school-trip pattern.

All 249 measured responses returned successfully, with zero `null` cells and
identical response hashes for each profile and size across deployments and
phases. Car was measured at all four sizes; foot was measured at 500 × 500.
The native 3,360 × 3,360 car response contained 162,465,839 bytes. The driver
verifies request hashes against the workload manifest before starting.

Timing starts before the HTTP POST and ends after the complete response has
been read and hashed in chunks. It includes server routing, serialization,
local transfer and client hashing, but excludes client JSON decoding. Warmup
requests are excluded from latency summaries. Fresh servers start for each
phase; the order of standalone and combined measurements alternates within
each phase. Both deployments remain resident during each phase, using the same
prepared graph files. Only one deployment receives timed traffic at a time.

## Environment and scope

- Apple M4 Max, 14 logical CPUs, 36 GiB RAM, macOS 26.5.1 arm64.
- Native OSRM v26.10.0, commit `4fc79f6f9fe314124164f6c2939335b76d395754`.
- Release build, Apple clang 17, assertions enabled, LTO disabled; mmap storage.
- Car and foot prepared sequentially with six threads from the same Norway PBF,
  with data timestamp `2026-08-19T20:20:48Z`.
- Source PBF copied from the existing `ferd-osrm-1` image; benchmark preparation
  and servers used separate local files and ports.
- Measured run: `2026-10-27T01:04:00Z` to `2026-10-27T01:06:48Z`.

Preprocessing finished before the timed run. One-minute host load averages
ranged from about 2.42 to 4.00 during measurements; unrelated local services
remained running. Five or six large-request samples per case support this
comparison, but not a reliable tail-latency or production-capacity estimate.
The raw sample p95 values are retained for transparency; with five or six
samples they equal the maximum observation.

This run covers CH car and foot with two workers and up to two concurrent
requests. Queueing under heavier concurrency, large foot matrices, bus profiles,
MLD, cold starts and sustained memory behavior need their own measurements.
The historical 4.6-second real Skoleskyss result is not a controlled comparison:
the locations, graph/build and exact timing boundary differ or are unspecified.

## Reproduction

The [benchmark driver](../../../scripts/benchmark_profile_tables.py) uses only
Python's standard library plus the adjacent server helper. The
[raw results](2026-10-27-oslo-akershus-matrix-benchmark.json) retain every measured
sample, workload manifest, host load observations and input/build hashes.

To use the already prepared files in this worktree, choose a fresh output file:

```sh
python3 scripts/benchmark_profile_tables.py --build build \
  --config build/benchmarks/oslo-akershus-2026-10-27/deployment.json \
  --workload example/multiple-profiles/oslo-akershus \
  --output build/benchmarks/oslo-akershus-2026-10-27/repeat.json \
  --workers 2 --samples 5 --mixed-rounds 3
```

For fresh preparation, place the Norway input at
`build/benchmarks/oslo-akershus/norway.osm.pbf` and save this configuration as
`build/benchmarks/oslo-akershus/deployment.json`. Its paths resolve relative to
that configuration file. The output directory must be fresh for preparation.

```json
{
  "version": 1,
  "input": "norway.osm.pbf",
  "output_directory": "release",
  "preparation": { "threads": 6 },
  "server": { "threads": 2, "storage": "mmap", "max_table_size": 4000 },
  "profiles": [
    { "name": "car", "lua": "../../../profiles/car.lua", "algorithm": "CH" },
    { "name": "foot", "lua": "../../../profiles/foot.lua", "algorithm": "CH" }
  ]
}
```

```sh
build/osrm-prepare --config build/benchmarks/oslo-akershus/deployment.json
python3 scripts/benchmark_profile_tables.py --build build \
  --config build/benchmarks/oslo-akershus/deployment.json \
  --workload example/multiple-profiles/oslo-akershus \
  --output build/benchmarks/oslo-akershus/results.json \
  --workers 2 --samples 5 --mixed-rounds 3
```

The driver starts and stops its own servers on ephemeral loopback ports. Its
first configured profile supplies the large requests; the second supplies the
small requests. It overrides serving options to use mmap, the requested worker
budget and a table limit equal to the largest saved request.
