# Multiple profile deployment validation

The native server serves independently prepared car and foot datasets through one
listener. The compatibility run passed 8,965 scenarios across the six CH/MLD and
mmap/direct/shared-memory combinations; 155 existing tagged scenarios were skipped.
The focused deployment matrix also covers CH/CH, MLD/MLD and CH/MLD with every
storage mode, all six services, existing POST methods, sequential switching and
concurrent requests. After the final access and descriptor checks, all 34
scenarios in the changed feature groups passed, alongside 326 affected C++ unit
cases, four Python dispatcher cases and 40 CLI parsing checks.

Validation used macOS arm64, Apple clang 17, Release with assertions enabled and
LTO disabled, the repository's pinned vcpkg dependencies, and Node 24.13.0. The
installed CLI example and editable Python preparation entry point ran successfully.
Windows output locking and Windows packages require their CI platform checks.

## Reproducing the comparison

Prepare the example once into a fresh output directory, then run:

```sh
build/osrm-prepare --config example/multiple-profiles/osrm.json
python3 scripts/benchmark_multiple_profiles.py --build build \
  --config example/multiple-profiles/osrm.json \
  --coordinates '7.4158,43.734132;7.421315,43.738814' --requests 30000
```

The driver uses a fixed total of two server workers, mmap storage, the same
Monaco datasets and route coordinates, 128 warmup requests per phase, persistent
HTTP connections and one sequential client. Single-profile standalone has two
workers; alternating standalone has two processes with one worker each. Combined
has one process with two workers. It checks byte-identical responses throughout
and across configurations. The cache correctness fix is present in every case.
Throughput includes HTTP/client overhead and periodic RSS sampling; this is not
a saturated engine throughput test. Peak memory is sampled RSS, summed across
standalone processes, rather than a peak-allocation measurement.

## Observations

Host load varied during these runs. Inspection during the repeat found multiple
unrelated CPU-intensive test workers. The same standalone single-profile query
varied from 0.120 ms to 0.560 ms median between long runs. These results establish
neither a dispatch regression nor a production performance gain. Repeat on an
otherwise idle host with representative regional data before capacity planning.
The small Monaco fixture cannot estimate large-graph cache reconstruction costs.

| Run | Configuration | Requests | Requests/s | Median ms | p95 ms | Peak sampled RSS KiB |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Initial | single standalone | 5000 | 7627.0 | 0.119 | 0.132 | 6512 |
| Initial | single combined | 5000 | 7518.8 | 0.119 | 0.132 | 6752 |
| Initial | alternating standalone | 5000 | 1931.6 | 0.151 | 0.221 | 13296 |
| Initial | alternating combined | 5000 | 6431.8 | 0.152 | 0.177 | 8288 |
| Long | single standalone | 30000 | 7622.6 | 0.12 | 0.168 | 6960 |
| Long | single combined | 30000 | 7686.0 | 0.119 | 0.158 | 6928 |
| Long | alternating standalone | 30000 | 2218.2 | 0.154 | 1.185 | 13360 |
| Long | alternating combined | 30000 | 757.3 | 0.7 | 4.889 | 8224 |
| Repeat | single standalone | 30000 | 1168.1 | 0.56 | 2.479 | 6496 |
| Repeat | single combined | 30000 | 8008.1 | 0.118 | 0.144 | 7056 |
| Repeat | alternating standalone | 30000 | 4327.5 | 0.151 | 0.258 | 13440 |
| Repeat | alternating combined | 30000 | 4613.0 | 0.198 | 0.334 | 8496 |

RSS samples levelled off or fell during the long switching runs; there was no
monotonic growth across those 30,000 requests. This is a bounded smoke observation,
not a proof of lifetime memory behavior. Combined storage still includes both
profiles' datasets. Query caches are cleared when a worker changes dataset views,
which trades reuse for correctness and can affect mixed-profile throughput.
