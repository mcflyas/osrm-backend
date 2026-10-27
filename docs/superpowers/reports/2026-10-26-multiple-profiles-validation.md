# Multiple profile deployment validation

The native server serves independently prepared car and foot datasets through one
listener. The compatibility run passed 8,965 scenarios across the six CH/MLD and
mmap/direct/shared-memory combinations; 155 existing tagged scenarios were skipped.
The focused deployment matrix also covers CH/CH, MLD/MLD and CH/MLD with every
storage mode, all six services, existing POST methods, sequential switching and
concurrent requests. After the final access and descriptor checks, all 34
scenarios in the changed feature groups passed, alongside 327 affected C++ unit
cases, four Python dispatcher cases and 40 CLI parsing checks.

Validation used macOS arm64, Apple clang 17, Release with assertions enabled and
LTO disabled, the repository's pinned vcpkg dependencies, and Node 24.13.0. The
installed CLI example and editable Python preparation entry point ran successfully.
The Debian trixie Linux arm64 image built with GCC 14.2.0, assertions disabled
and LTO enabled. Its installed preparation, mixed CH/MLD HTTP dispatch,
`InvalidProfile`, and shared-memory startup with the release files hidden all
passed. Windows output locking and Windows packages require their CI platform checks.

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

## Review and final corrections

A fresh whole-branch review confirmed all five design review focus points and
found one optimization opportunity: CH scratch heaps could potentially survive
profile switches. That optimization is deferred pending representative measurements.

The review also identified inherited unpublished-region cleanup as a follow-up.
It was promoted to a required correction because repeated failed publications
could exhaust shared-memory reservations. A corrupt-graph regression first
observed both a retained region and a leaked key, then passed after adding scoped
reservation ownership. Published regions retain their existing lifetime; failed
pre-publication work and lock timeouts release pending reservations.

The Linux release build exposed an assertion-only loop variable rejected by
`-DNDEBUG -Werror`. Expressing the condition as one `all_of` assertion fixed the
build. The affected native suites and complete container smoke workflow were
rerun after both corrections.

## Implementation decisions and tradeoffs

- use /private/tmp/osrm-multiple-profiles for the worktree — writable, isolated, avoids adding worktree ignores on master — cost if wrong: user must open the linked worktree to inspect implementation.
- dependency installation and baseline build precede tests — no existing build or node_modules are present — cost if wrong: setup time; no behavior changes.
- fix shared CLI path validation ADL and restrict extension stripping to path suffixes — real preparation/legacy trial tests exposed valid paths being rejected or changed — cost if wrong: existing tools share these helpers, so storage tests and full command matrix must cover regressions.
- format changed C++ files in a staging mirror using the exact project format.sh — the full-tree script reformats unrelated existing files — cost if wrong: staged formatting misses cross-file formatting effects (clang-format is file-local).
- config-mode datastore embeds both road and area spatial-index leaves — stock shared memory stores disk paths, and hiding the release reproduced startup failure — cost if wrong: shared-memory size grows by leaf-file sizes; legacy publication keeps its existing layout.
- propagate swapData timeout failure and release reserved keys — regression observed return 0 and two leaked reservations after a failed swap — cost if wrong: legacy callers now receive the truthful nonzero failure status.
- use per-test namespaces and a test-only scoped cleanup executable — global spring-clean would affect unrelated datasets — cost if wrong: tests need their cleanup target built.
- split the held-facade and actual-engine refresh checks — two watchdogs plus in-process publication exceeded macOS kern.sysv.shmseg=8; separate checks preserve coverage — cost if wrong: macOS multi-engine resource pressure still needs deployment sizing. Recovered only the failed test namespace after verifying registry contained no unrelated datasets.
- correct legacy datastore no-argument help assertion to stdout — existing code returns help on stdout, whereas the assertion expected an unrelated stderr parse error — cost if wrong: test no longer enforces that obsolete error text; intended help behavior remains tested.
- load stored CH/MLD connectivity checksums into process/shared memory — matrix exposed zero-valued hints versus mmap, reproduced in focused CH+MLD unit tests — cost if wrong: old memory-mode hints change, but now identify the actual dataset.
- include build and build-* directories in Python editable CLI lookup — actual python -m osrm prepare could not find the successfully built binary, reproduced with a temporary editable layout — cost if wrong: another valid local build can be selected by the documented fallback.
- report performance samples without a speed claim — repeated measurements and observed unrelated CPU load prevent an isolated latency conclusion — cost if wrong: deployment sizing still requires an idle-host benchmark on representative data.
- keep build dependencies under build/development-tools rather than deleting them with planning scratch files — preserves the usable CMake/Python environment — cost if wrong: local build caches require regeneration; no repository dependency versions change.
- overlap the fresh whole-branch review with the remaining Linux package build — implementation is committed and native checks passed; completion still requires both results — cost if wrong: a platform fix must be included in the single tested final fix pass.
- promote inherited unpublished shared-memory reservation leaks to Important — repeated failed profile publications can exhaust keys and retain memory, so fix ownership-based cleanup — cost if wrong: legacy failure cleanup changes as well and needs full storage/library regression coverage.
- Windows execution remains unverified — this macOS/Linux environment cannot establish Windows locking and package behavior — cost if wrong: Windows-specific defects require CI before release.
- production-scale latency and capacity remain unverified — small fixtures on a contended host support correctness and bounded smoke observations only — cost if wrong: users must benchmark representative data before sizing.
