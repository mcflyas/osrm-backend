# Multiple profiles in one OSRM deployment

Design for preparing several routing profiles from one configuration and serving them from one `osrm-routed` process. The purpose is to remove the need to maintain a separate serving deployment for each profile. Each profile retains its own prepared routing data.

## Agreed direction

The user chose native support in `osrm-routed`, accepted restarting the server when adding or removing a profile, and requested that the same configuration also drive preparation. Car, foot, and bus are examples of profiles to serve.

The user accepted the preparation and release workflow, including sequential builds into fresh release directories. JSON configuration, one storage mode per deployment, and requiring every profile to load were presented as defaults with that workflow. The detailed interfaces below are implementation choices made by the assistant within that direction. This is a design, not an implemented feature.

## Approaches

| Approach | Benefit | Cost and decision |
| --- | --- | --- |
| One native server hosting independent engines | One endpoint, configuration, worker pool, and deployment lifecycle | Requires configuration, dispatch, and cache isolation changes. Selected by the user. |
| Package existing servers behind a proxy | Reuses the existing server interface and isolates process failures | Keeps several processes, internal ports, and a proxy to maintain. Not selected. |
| Share one graph across transport modes | Could reduce some storage and preparation work | Requires addressing different accessible edges, directions, restrictions, and graph structures. Does not directly solve the deployment problem and is outside this design. |

## Scope

Provide a shared configuration reader, an `osrm-prepare` command, and multi-profile dispatch in `osrm-routed`. Add configuration support to `osrm-datastore` to load or refresh the configured shared-memory datasets with one command. Preparation and serving remain separate operations so the server can use a read-only data mount and start without access to OSM input or Lua files.

Support CH and MLD, including different algorithms in the same process. Retain file loading into process memory, mmap, and named shared memory. Preserve existing single-profile command behavior. The C++, Python, and Node routing APIs can continue representing one engine each; a new public multi-profile library API is unnecessary for this scope.

Do not add shared graph extraction, mixed-mode journeys, public transport schedules, dynamic profile registration, automatic OSM downloads, or a deployment scheduler. A bus entry points to a supplied Lua profile; this work does not define bus routing semantics or add a new bus profile. Avoid changing existing Lua profiles just to introduce orchestration.

## Configuration

Use a versioned JSON file, parsed with the existing RapidJSON dependency. A minimal example is:

```json
{
  "version": 1,
  "input": "./maps/region.osm.pbf",
  "output_directory": "./data/release-001",
  "preparation": {
    "threads": 8
  },
  "server": {
    "ip": "0.0.0.0",
    "port": 5000,
    "threads": 8,
    "storage": "mmap"
  },
  "profiles": [
    { "name": "car", "lua": "./profiles/car.lua", "algorithm": "MLD" },
    { "name": "foot", "lua": "./profiles/foot.lua", "algorithm": "MLD" },
    { "name": "bus", "lua": "./custom/bus.lua", "algorithm": "CH" }
  ]
}
```

Paths are resolved relative to the configuration file, independent of the working directory. Each profile's dataset base is `<output_directory>/<name>/map.osrm`. The public name identifies the URL segment; it need not match the Lua filename. Names are case-sensitive, nonempty, and restricted to ASCII letters, digits, underscores, and hyphens so they are safe directory components. Reject duplicate names, duplicate JSON keys, unknown keys, unsupported versions, empty profile lists, invalid algorithms, and invalid numeric values with errors naming the configuration field.

`version`, `output_directory`, and a nonempty `profiles` array are required. Each profile requires `name` and `algorithm`. `input` and profile `lua` are required for preparation but optional for a serving-only configuration. When present, their types are validated by every consumer, but only preparation checks that those files exist. `server` and `preparation` are optional. Server IP, port, threads, timeouts, and request limits use the existing `osrm-routed` defaults; configuration mode defaults storage to `mmap`. Preparation threads default to at least one hardware thread. Algorithm selection is explicit per profile.

The server configuration accepts the existing server-wide timeouts, request size limits, and query limits under documented JSON names. Those limits apply to every profile in the first version. Feature dataset disabling remains available as a global engine setting. Storage mode is global, with values `mmap`, `memory`, or `shared_memory`. In shared-memory mode, require a nonempty `dataset_namespace`; derive the dataset name as `<dataset_namespace>/<profile name>`. Validate resulting names against storage constraints and keep different deployment namespaces distinct.

Explicit server-wide CLI settings override the corresponding configuration fields. Reject ambiguous combinations of `--config` with legacy positional datasets or single-engine options such as `--algorithm`, `--dataset-name`, `--shared-memory`, `--mmap`, and `--memory_file`. `--help`, `--version`, and `--trial` retain their meanings. The old invocation without `--config` keeps its existing semantics, including its handling of arbitrary URL profile labels.

Use one parser and normalized configuration model across all three commands. Separate structural validation from each command's input and artifact validation. Do not expose unrestricted shell command strings or arbitrary stage argument lists in the initial schema. Existing preparation tools remain available for advanced workflows; adding typed preparation options later is an extension of this schema.

## Preparation

Proposed commands:

```sh
osrm-prepare --config osrm.json
osrm-routed --config osrm.json
```

`osrm-prepare` is an installed C++ executable using the existing extraction, partitioning, contraction, and customization libraries. This avoids requiring Python or Node in the runtime image. It links preparation libraries independently of the routing server. Preserve executable symbol exports required by Lua modules and apply existing tool validation and defaults when constructing each stage's configuration.

Validate all profile definitions, input files, Lua paths, and the destination before beginning. Require the output directory to be absent or empty. Reject nonempty destinations rather than overwriting datasets that a server may have mapped. A rebuild uses a new release directory by changing `output_directory` in the configuration. Deployment automation remains responsible for retaining or deleting old releases and restarting file-backed servers against the new configuration.

Prepare profiles sequentially in configuration order, passing the configured thread budget to each stage. This bounds simultaneous preparation memory and avoids multiplying the thread budget by the number of profiles. For CH, run extraction followed by contraction. For MLD, run extraction, partitioning, then customization. Optional partitioning before CH contraction is deferred; the established CH pipeline remains valid without it.

Write a completion descriptor only after every profile finishes and its required artifact set is validated. Record the descriptor version, ordered names, algorithms, and relative dataset bases. Publish it with a temporary file and rename in the same directory. Configuration-mode file serving and datastore loading require this descriptor to agree with the configured names and algorithms before opening datasets. The descriptor establishes successful preparation, not a content-addressed cache or proof that files have never changed. Normal storage and algorithm validation still run.

On failure, stop at the failing stage, report the profile and stage, and return nonzero. Leave the incomplete output directory for diagnosis without a completion descriptor. Retrying uses another empty directory or an explicit operator cleanup; the first version has no resume or incremental build logic. Serialize preparation attempts against the same output directory using an exclusive lock, held through descriptor publication. The lock must release when its owner exits without requiring a persistent success state.

Profile selection, parallel builds, content-hash caching, automatic replacement of live files, and automatic release cleanup are deferred. Reading the same OSM file once per profile is expected.

## Serving

Add a handler implementing the existing `ServiceHandlerInterface` that owns a registry from profile name to the existing `ServiceHandler`. Each child handler owns its `OSRM` engine and service objects. Construct the registry completely before the HTTP listener becomes ready, and retain it until shutdown.

Dispatch using `ParsedURL.profile`, then delegate service and version validation and parameter parsing to the selected child. Cover every existing service and every method currently supported by that service, including GET and POST paths. Do not add POST support to services that lack it. Preserve URL prefix lengths for parse errors. Return HTTP 400 with JSON code `InvalidProfile` for an unknown name in configuration mode; never silently substitute another profile. Existing single-profile mode retains its current error behavior.

Every profile shares the configured worker pool, so switching profiles on the same thread is normal. Engine and service ownership must be stable for concurrent requests. A request acquires one dataset facade and retains it for the request, including when a shared-memory update occurs. Adding or removing a registry entry requires restarting the server.

If any configured engine cannot load or has incompatible data, fail startup with the profile name and underlying error. `--trial` validates and initializes every configured engine, then exits. Signal readiness only after all profiles are usable. Log profile names and algorithms during initialization and dataset refresh; request URLs already identify the selected profile.

## Shared memory and updates

For shared-memory mode, the proposed startup sequence is:

```sh
osrm-prepare --config osrm.json
osrm-datastore --config osrm.json
osrm-routed --config osrm.json
```

`osrm-datastore --config` validates the completion descriptor and loads the prepared profiles under their derived dataset names. This command requires `server.storage` to be `shared_memory`. It does not prepare data. Existing single-dataset datastore commands continue supporting targeted refreshes and metric updates.

Rebuilding into a new output directory and running datastore with the updated configuration refreshes registered profiles through the existing watchdog mechanism. A running server retains its profile registry and namespace; it does not need to reread the new output directory because its engines attach by shared-memory name. It does not require the preparation descriptor or any local preparation files. Changing the profile list, namespace, storage mode, or algorithms requires a restart.

Datastore processes profiles sequentially and stops on the first failure. Each successful publication is independent; an error may leave earlier profiles updated and later profiles unchanged. Report which profiles were published. Atomic publication across all profiles is outside the first version. Respect the existing shared-memory capacity limits and report capacity errors with the affected profile. Loading shared memory is an explicit command, never a side effect of preparing files or starting the server.

## Cache correctness

Dataset isolation is required before enabling shared workers. In the current checkout, `SearchEngineData<MLD>::unpacking_cache` is static thread-local state. `InitializeUnpackingCache` keeps it when node and edge counts match; its entries use graph-local node, edge, and cell identifiers without identifying the dataset or metric. Thus the code permits reuse across distinct graphs of equal size. This is a code-inspection finding; no failing route has yet been reproduced in this design work.

The implementation must associate persistent graph-dependent caches with a stable dataset generation and selected metric or exclusion view. Refreshing weights or changing exclusions can change cached paths without changing graph sizes. A raw facade pointer alone is insufficient unless lifetime and address reuse are handled. Prefer clearing the active per-thread cache when the identity changes for the first implementation; correctness takes priority over retaining caches for every profile. The implementation may choose another bounded strategy with the same guarantees.

Review all static and thread-local routing state, including search heaps, area routing caches, and spatial-query scratch buffers. Scratch storage may be reused after a proper reset, but its capacities and interpretation must remain valid when switching graph sizes or algorithms. Avoid asserting that every heap needs reallocation: current storage implementations differ. Keep caches bounded per worker rather than multiplying a graph-sized allocation by every profile and worker without measurement.

## Validation

Test the shared parser's valid and invalid configurations, path resolution, CLI precedence, command-specific file requirements, and legacy invocations. Test preparation failure, locking, absence of a completion descriptor after failure, nonempty destination refusal, and artifact compatibility checks.

Use a small fixture with deliberately different car and foot access and costs. Prepare both through one configuration and compare every service against its standalone engine. Cover dispatch for GET and existing POST services, unknown profiles, mixed CH and MLD entries, startup failures, and every supported loading mode. Tests can select existing profiles or test profiles without changing their semantics.

Exercise alternating requests on one worker and concurrent requests on several workers. Include different datasets with equal node and edge counts but different weights or paths, different graph sizes, changing exclusion views, and shared-memory weight updates. The first cache regression must demonstrate the failure before changing cache behavior. Verify that refreshing one profile does not change another and that in-flight requests retain a coherent facade.

Measure single-profile overhead and alternating-profile throughput and memory against standalone servers. No memory reduction or throughput improvement is promised; the expected benefit is simpler deployment. The acceptance condition is correct profile selection and equivalent per-profile answers with bounded resource use.

## Integration and documentation

Install `osrm-prepare` through CMake and include it in shipped binary artifacts and the container smoke checks. Audit Python and Node distribution rules so a new installed tool is packaged wherever other preparation tools are shipped. Add `prepare` to the Python command dispatcher if packaged there, and update `docs/python/development.md` with packaging or build changes. Routing binding signatures remain outside scope.

Document the schema, preparation and serving commands, file and shared-memory release procedures, expected disk and memory use, and single-profile compatibility in the existing profiles, tools, and HTTP documentation. Update the HTTP error reference for `InvalidProfile`. Include a runnable car and foot example; label any bus example as requiring a user-supplied Lua profile.

## Remaining implementation decisions

The builder can choose the internal configuration type and file layout, specific locking and atomic descriptor helpers, test fixture construction, and the cache generation representation. These choices must satisfy the observable behavior above. Advanced preparation parameters and per-profile query limits require later schema extensions rather than undocumented pass-through behavior.

The implementation plan should preserve JSON configuration, separate preparation and serving commands, sequential full builds into fresh release directories, one storage mode per deployment, and failing startup if any configured profile cannot load.

## Code references

- [Service ownership and dispatch](../../../src/server/service_handler.cpp)
- [Parsed URL profile field](../../../include/server/api/parsed_url.hpp)
- [Server handler registration](../../../include/server/server.hpp)
- [Server startup and existing options](../../../src/tools/routed.cpp)
- [Engine configuration and data providers](../../../include/engine/engine.hpp)
- [Shared-memory dataset watchdog](../../../include/engine/data_watchdog.hpp)
- [Persistent MLD cache initialization](../../../src/engine/search_engine_data.cpp)
- [MLD cache lookup and path reuse](../../../include/engine/routing_algorithms/routing_base_mld.hpp)
- [Existing preparation entry points](../../../src/tools/extract.cpp)
- [CMake libraries and installed executables](../../../CMakeLists.txt)
