# Routing server

## Multiple profiles

Use one deployment configuration to prepare separate datasets and serve them from
one `osrm-routed` process:

```sh
osrm-prepare --config osrm.json
osrm-routed --config osrm.json
```

```json
{
  "version": 1,
  "input": "maps/region.osm.pbf",
  "output_directory": "data/release-001",
  "preparation": { "threads": 8 },
  "server": { "threads": 8, "storage": "mmap", "port": 5000 },
  "profiles": [
    { "name": "car", "lua": "profiles/car.lua", "algorithm": "MLD" },
    { "name": "foot", "lua": "profiles/foot.lua", "algorithm": "CH" }
  ]
}
```

Every path is relative to the configuration file. Profile names are case-sensitive
ASCII letters, digits, underscores and hyphens. Names must be unique and nonempty.
An entry's public name need not match its Lua filename. Each dataset base is
`<output_directory>/<name>/map.osrm`. The example repository configuration is
[here](../example/multiple-profiles/osrm.json).

### Configuration reference

Unknown fields, duplicate JSON keys, invalid types, embedded NULs, unsupported
versions and invalid values are errors. No command accepts shell commands or
arbitrary preparation arguments in this configuration.

| Field | Default or requirement |
| --- | --- |
| `version` | Required integer `1`. |
| `output_directory` | Required nonempty path. Must be absent or empty for preparation. |
| `input` | OSM file path; required and checked only for preparation. |
| `preparation.threads` | Logical CPU count, at least `1`; positive unsigned integer. |
| `profiles` | Required nonempty array. |
| `profiles[].name` | Required unique public name. |
| `profiles[].algorithm` | Required `CH` or `MLD`. |
| `profiles[].lua` | Lua file path; required and checked only for preparation. |

All fields below belong to `server` and apply to every profile.

| Field | Default | Accepted values |
| --- | --- | --- |
| `ip` | `0.0.0.0` | Nonempty bind address. |
| `port` | `5000` | Integer `0..65535`; `0` selects an available port. |
| `threads` | Logical CPUs, at least `1` | Positive integer. One worker pool serves all profiles. |
| `keepalive_timeout` | `5` | Seconds, integer `1..32767`. |
| `storage` | `mmap` | `mmap`, `memory`, or `shared_memory`. |
| `dataset_namespace` | None | Required for shared memory; names become `<namespace>/<profile>`. Each name plus `/updatable` must fit 254 bytes. |
| `verbosity` | `INFO` in release builds, `DEBUG` otherwise | `NONE`, `ERROR`, `WARNING`, `INFO`, `DEBUG`. |
| `max_viaroute_size` | `500` | `-1` or integer greater than `2`. |
| `max_trip_size` | `100` | `-1` or integer greater than `2`. |
| `max_table_size` | `100` | `-1` or integer greater than `2`. |
| `max_matching_size` | `100` | `-1` or integer greater than `2`. |
| `max_nearest_size` | `100` | `-1` or positive integer; result count. |
| `max_nearest_locations` | `10` | `-1` or positive integer; input location count. |
| `max_alternatives` | `3` | Nonnegative integer. |
| `max_matching_radius` | `-1` | Positive metres, `-1`, or `"unlimited"`. |
| `default_radius` | `-1` | Positive metres, `-1`, or `"unlimited"`. |
| `max_header_size` | `0` | Unsigned 32-bit byte count; `0` derives a limit from final query limits. |
| `max_request_body_size` | `0` | Unsigned 64-bit byte count; `0` derives a limit from final query limits. |
| `disable_feature_dataset` | `[]` | Array of `ROUTE_STEPS` and/or `ROUTE_GEOMETRY`; lowercase spellings also accepted. |

For query limits, `-1` means unlimited. Automatic HTTP size limits remain finite;
set explicit sizes when large requests need them. Feature disabling retains the
existing service limitations.

Explicit server-wide CLI options override the corresponding JSON fields. JSON
names replace CLI hyphens with underscores. For example, `--port 5102
--max-table-size 456` overrides only those two values. Automatic HTTP sizes use
the final limits. Combining `--config` with a positional dataset, `--algorithm`,
`--dataset-name`, `--shared-memory`, `--mmap` or `--memory_file` is an error.

`osrm-routed --config osrm.json --trial` initializes every engine and exits without
opening a listener or signaling readiness. Normal startup fails if any profile
cannot load and identifies the failing profile. `--help` and `--version` work
without loading configuration.

### Preparation and file releases

Preparation validates all inputs, reserves an exclusive output lock, and creates
all profile directories before running a stage. Filesystem aliases such as `car`
and `Car` on a case-insensitive filesystem are rejected. The lock releases when
the process exits; the adjacent `.prepare.lock` file can remain on disk.

Profiles build sequentially with one thread budget. CH uses extract/contract;
MLD uses extract/partition/customize. After every artifact set is present,
preparation atomically renames a temporary completion file to `prepared.json`.
The descriptor contains version `1` and the ordered profile names, algorithms,
and relative dataset bases. File-backed config serving requires that descriptor
to match, then loads and validates the datasets normally.

A failure returns nonzero with the profile and stage and leaves diagnostic files
without a completion descriptor. There is no resume or automatic cleanup. Use a
new output directory, or explicitly remove the failed release after diagnosis.
Do not overwrite a release that running servers have mapped. Switch file-backed
servers to a newly prepared release by restarting them with its configuration.
Serving-only configurations may omit `input` and every `lua` field.

### Shared-memory releases

Set `server.storage` to `shared_memory` and choose a deployment-specific
`server.dataset_namespace`, then run:

```sh
osrm-prepare --config osrm.json
osrm-datastore --config osrm.json
osrm-routed --config osrm.json
```

Datastore validates the completion descriptor and publishes profiles sequentially.
Config-mode publication includes road and pedestrian-area spatial-index leaves in
shared memory. The configured server needs no local release files, OSM input, or
Lua scripts. Existing single-dataset datastore invocations retain their disk-backed
leaf indexes. Use compatible binaries for publisher and server.

To refresh, prepare a fresh output directory and publish it with the same namespace,
names and algorithms. Running servers attach by name and observe each update; a
request retains its acquired dataset view until it finishes. A targeted legacy
`--only-metric` update remains available for compatible topology.

Publication is **not atomic across profiles**. A later failure leaves earlier
successful publications in place. The command reports successes and the failing
profile and stops. Restart affected consumers when changing algorithms, and restart
the server when changing names, namespace or storage mode. Config publication rejects
positional datasets, `--dataset-name`, `--only-metric`, `--list`, `--list-blocks`,
`--remove-locks` and `--spring-clean`. `--verbosity`, `--max-wait` and global
`--disable-feature-dataset` overrides remain available.

### Resource use and compatibility

This consolidates process and deployment management. Extraction still runs once
per profile, disk use accumulates across datasets, and serving memory includes
each engine's data. Configured shared memory also includes spatial-index leaves.
OS shared-memory limits still apply; use `--trial` to check a deployment on its
target host. Query scratch storage is bounded per worker and algorithm; switching
dataset views clears graph-dependent caches and can reduce throughput.

All six services dispatch by the URL profile segment. Existing POST support is
preserved. An unknown configured name returns HTTP 400 with `InvalidProfile`.
Legacy `osrm-routed <base.osrm>` keeps its single-engine behavior and accepts
arbitrary URL profile labels as before. The C++, Python and Node routing objects
continue to represent one engine each.

## Environment Variables

### SHM_LOCK_DIR

If the SHM_LOCK_DIR environment variable is set, OSRM will use it as the
directory for shared memory lock files instead of the system temporary directory.
This is useful in containerized environments (Docker/Kubernetes) where the lock
file directory should persist across container restarts when loading from shared
memory.

### SIGNAL_PARENT_WHEN_READY

If the SIGNAL_PARENT_WHEN_READY environment variable is set osrm-routed will
send the USR1 signal to its parent when it will be running and waiting for
requests. This could be used to upgrade osrm-routed to a new binary on the fly
without any service downtime - no incoming requests will be lost.

### DISABLE_ACCESS_LOGGING

If the DISABLE_ACCESS_LOGGING environment variable is set osrm-routed will
**not** log any http requests to standard output. This can be useful in high
traffic setup.
