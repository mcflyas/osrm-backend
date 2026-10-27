# Multiple Profiles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare independently routed profiles from one configuration and serve them through one native OSRM deployment.

**Architecture:** A shared configuration and completion descriptor connect a new preparation command, named datastore publication, and a profile-dispatching server handler. Each profile owns an existing engine. Dataset facade ownership isolates persistent caches when worker threads switch profiles, metrics, or generations.

**Tech Stack:** C++20 with the repository's compiler compatibility constraints, RapidJSON, Boost.Program_options, Boost.Test, CMake, and cucumber-js.

**Spec:** [Multiple profiles in one OSRM deployment](../specs/2026-10-26-multiple-profiles-design.md).

## Global Constraints

- Each profile retains its own prepared routing data.
- Adding or removing a registry entry requires restarting the server.
- Preserve existing single-profile command behavior.
- Storage mode is global, with values `mmap`, `memory`, or `shared_memory`.
- Require the output directory to be absent or empty.
- Atomic publication across all profiles is outside the first version.
- Use the existing RapidJSON dependency and installed C++ preparation libraries; no new runtime language dependency.
- Follow `AGENTS.md`: C++20, no new `using namespace`, four-space indentation, new functions in lowerCamelCase, public members in UpperCamelCase, warnings as errors, and `BOOST_ASSERT` for internal assumptions. Existing virtual overrides retain their established names.
- Use `./scripts/format.sh` with clang-format-22 for implementation. Inspect its repository-wide output and retain only changes belonging to the work. No Lua behavior change is planned; any such change triggers the repository's profile test and taginfo requirements.
- Work in an isolated checkout at execution time. No implementation, build, or benchmark has been performed while writing this plan.

## Review Focus

1. CLI options carrying defaults must not overwrite values explicitly provided by the configuration; Task 4 tests omitted and explicit overrides.
2. Two distinct profile names can alias the same directory on a case-insensitive filesystem; Task 3 must reject the collision before any extraction.
3. Equal-sized datasets, exclusion views, and updated metrics can have different paths; Task 1 proves cache isolation independently of graph counts and pointer addresses, with refresh coverage in Task 5.
4. Some public preparation wrappers discard integer return codes; Task 3 must detect stage failure and never publish a completion descriptor after it.
5. Shared-memory servers must start without local data files, while names must fit the shared-memory registry including its suffixes; Tasks 2 and 5 cover both requirements.

---

## File boundaries and task order

Keep common deployment parsing, descriptor validation, and datastore publication under `include/deployment/` and `src/deployment/`. Build these as an internal `osrm_deployment` static library linked to `osrm_store`; it must not pull extraction or routing engines into every consumer. Preparation orchestration and output-directory ownership form a separate internal `osrm_preparation` library linked only by preparation and its tests. Neither library is a new installed public API.

The existing engine, server, and tool modules retain their responsibilities. Add a narrow `MultiProfileServiceHandler`, rather than moving parameter parsers or algorithm code into the dispatcher. Unit-test deployment code with a new `deployment-tests` target; exercise real commands with cucumber features and the existing process helpers.

Execute Tasks 1–6 in order. Task 1 is independently useful as a correctness fix. Tasks 2–5 share interfaces and constitute one feature; splitting them into unrelated project plans would obscure those dependencies. Task 6 delivers packaging, operator documentation, and the final comparison against standalone engines.

Use a configured repository build directory, represented below by `BUILD_DIR`. Re-run CMake after adding sources because the existing source globs are not automatic discovery watches. Build targets with `cmake --build "${BUILD_DIR}" --target <targets> -j "$(nproc --ignore=2)"`; on a host lacking `nproc`, use an equivalent positive local CPU count with two cores reserved. New unit targets must be registered with the existing CTest fixture pattern.

### Task 1: Isolate routing state across dataset views

**Files:**
- Modify: `include/engine/search_engine_data.hpp`, `src/engine/search_engine_data.cpp`, `include/engine/engine.hpp`.
- Test: new `unit_tests/engine/search_engine_data.cpp`, new `unit_tests/library/multiple_datasets.cpp`.
- Fixture: new `test/data/multiple_profiles.osm`, modify `test/data/Makefile` and `unit_tests/CMakeLists.txt`.
- Inspect: `src/engine/area_geodesic.cpp`, `include/util/static_rtree.hpp`, `include/util/query_heap.hpp`, and `include/engine/datafacade_factory.hpp`.

**Interfaces:**
- Consumes: each request's `std::shared_ptr<const DataFacade<Algorithm>>` from `DataFacadeProvider::Get`.
- Produces: `void SearchEngineData<Algorithm>::selectDataset(const std::shared_ptr<const void> &dataset)` for both CH and MLD specializations. Existing heap and unpacking-cache initialization signatures remain available.
- Identity: a static thread-local `std::weak_ptr<const void>` per algorithm observes the selected facade's ownership. Equal ownership means neither weak pointer orders before the other; expiration or different ownership means a new view. Do not retain a strong owner merely to preserve a cache.

- [ ] **Step 1: Reproduce cross-dataset path reuse before changing routing state.** Create a deterministic graph with two alternative corridors and enough cells to require MLD overlay unpacking. Partition with small `--max-cell-sizes` values in the test fixture. Copy the extracted and partitioned topology to A and B, then customize different segment speeds so the preferred corridors differ while node and edge counts match. Derive standalone expected routes with caches reset between baselines. In `multiple_datasets/alternating_equal_sized_metrics`, compare A, B, A on the same thread against those baselines:

  ```cpp
  BOOST_REQUIRE_EQUAL(a_node_count, b_node_count);
  BOOST_REQUIRE_EQUAL(a_edge_count, b_edge_count);
  BOOST_REQUIRE(expected_a != expected_b);
  BOOST_CHECK_EQUAL(runRoute(engine_a), expected_a);
  BOOST_CHECK_EQUAL(runRoute(engine_b), expected_b);
  BOOST_CHECK_EQUAL(runRoute(engine_a), expected_a);
  ```

  Define test-local `std::string runRoute(osrm::OSRM &engine)` to query fixed fixture endpoints and serialize the route geometry and annotations; avoid comparing elapsed query time. The fixture must demonstrate an actual incorrect result or assertion on current code. If it does not, refine the fixture rather than treating a missing new API as the reproduction.

- [ ] **Step 2: Build test data and run the regression.** Build `test-data` and `library-tests`, then run `"${BUILD_DIR}/unit_tests/library-tests" --run_test=multiple_datasets/alternating_equal_sized_metrics`. Record the observed pre-fix failure.
- [ ] **Step 3: Add ownership-boundary tests and implement selection.** Acquire the facade once inside `Engine::GetAlgorithms`, call `selectDataset` if non-null, and pass that same facade into `RoutingAlgorithms`. On an ownership change, reset the algorithm's heaps and persistent cache, including cache sizing metadata; lazy initialization recreates them for the selected graph. Repeated selection of the same owner preserves storage. A null facade continues through the existing invalid-exclusion path without dereferencing it.

  Add `search_engine_data/same_owner_preserves_cache`, `different_owner_clears_cache`, and `expired_owner_clears_cache`. Insert a nonempty cache entry and assert that it survives only the same-owner case. Use separate owners with equal graph sizes; test expiration explicitly. Add route regressions for different graph sizes and alternating exclusion views. Audit the area and spatial scratch caches, documenting why existing resets or content checks suffice, or add a targeted fix and reproduction if they do not.

- [ ] **Step 4: Verify.** Build `engine-tests` and `library-tests`, then run both full executables. The route regression passes, repeated same-profile queries retain their cache, and heap state does not cross ownership boundaries. Run the focused regression under an available sanitizer configuration if the audit finds a bounds or lifetime concern.
- [ ] **Step 5: Commit the independently verified change.** Suggested message: `fix: isolate routing caches by dataset view`.

### Task 2: Define configuration and completion descriptors

**Files:**
- Create: `include/deployment/config.hpp`, `src/deployment/config.cpp`, `include/deployment/descriptor.hpp`, `src/deployment/descriptor.cpp`.
- Create: `unit_tests/deployment_tests.cpp`, `unit_tests/deployment/config.cpp`, `unit_tests/deployment/descriptor.cpp`.
- Modify: `CMakeLists.txt`, `unit_tests/CMakeLists.txt`.

**Interfaces:** All new symbols below are in `osrm::deployment`.
- `enum class StorageMode { MMap, Memory, SharedMemory };`
- `enum class ConfigPurpose { Prepare, Serve, Store };`
- `ProfileConfig`: public fields `std::string Name`, `std::filesystem::path Lua`, and `engine::EngineConfig::Algorithm Algorithm`.
- `ServerOptions`: `Ip`, `Port`, `Threads`, `KeepaliveTimeout`, `MaxHeaderSize`, `MaxBodySize`, `Storage`, `DatasetNamespace`, and `EngineDefaults`. Use the existing routed types and ranges for each corresponding field; `Storage` is `StorageMode` and `EngineDefaults` is `engine::EngineConfig` holding shared query limits, verbosity, and feature settings.
- `DeploymentConfig`: public `Input`, `OutputDirectory` as filesystem paths, `PreparationThreads` as unsigned, `Server` as `ServerOptions`, and `Profiles` as `std::vector<ProfileConfig>`.
- `ServerOptions defaultServerOptions();`
- `std::optional<DeploymentConfig> readConfig(const std::filesystem::path &path, std::string &error);`
- `bool validateConfig(const DeploymentConfig &config, ConfigPurpose purpose, std::string &error);`
- `std::filesystem::path datasetBase(const DeploymentConfig &config, const ProfileConfig &profile);`
- `std::string datasetName(const DeploymentConfig &config, const ProfileConfig &profile);`
- `engine::EngineConfig makeEngineConfig(const DeploymentConfig &config, const ProfileConfig &profile);`
- `bool writeCompletionDescriptor(const DeploymentConfig &config, std::string &error);`
- `bool validatePreparedData(const DeploymentConfig &config, std::string &error);`

- [ ] **Step 1: Write configuration tests.** Cover the spec's JSON example without requiring its paths to exist during structural parsing. Assert relative paths use the configuration directory, default storage is mmap, algorithms are per profile, and omitted input/Lua fields are accepted for serving but rejected for preparation. Reject duplicate keys at every object level, duplicate names, unknown fields, invalid types, embedded NULs in strings used as paths or names, and nonpositive thread counts. Reject names outside `[A-Za-z0-9_-]+`.

  ```cpp
  BOOST_CHECK(config.Server.Storage == StorageMode::MMap);
  BOOST_CHECK_EQUAL(config.Server.Port, 5000);
  BOOST_CHECK_EQUAL(config.Server.EngineDefaults.max_locations_viaroute, 500);
  BOOST_CHECK_EQUAL(datasetBase(config, config.Profiles[0]), config.OutputDirectory / "car/map.osrm");
  BOOST_CHECK(!validateConfig(serving_only_config, ConfigPurpose::Prepare, error));
  ```

  A fixture owns each temporary configuration directory. Include paths with spaces and Unicode. For shared memory, require a nonempty `server.dataset_namespace` and ensure both derived `/static` and `/updatable` region names fit `SharedRegion::MAX_NAME_LENGTH`; do not rely on storage's truncation behavior.

- [ ] **Step 2: Add descriptor tests and run them failing.** Fix the filename as `prepared.json` and the schema as `{"version":1,"profiles":[{"name":"car","algorithm":"MLD","dataset":"car/map.osrm"}]}` in configuration order. Reject missing, incomplete, reordered, mismatching, and unsupported descriptors. The `dataset` value must equal the derived relative path, not supply an arbitrary alternative. Build and run the new `deployment-tests`; initial missing implementations or failed assertions are expected.
- [ ] **Step 3: Implement the shared reader and helpers.** JSON keys follow the spec; server-wide CLI names map to JSON by replacing hyphens with underscores (`max_viaroute_size`, `max_request_body_size`, `disable_feature_dataset`, etc.). Include `verbosity`. Preserve the routed defaults: `0.0.0.0`, port 5000, hardware threads clamped to at least one, keepalive 5, auto header/body sizes 0, route limit 500, trip/table/match/nearest-result limits 100, nearest-location limit 10, alternatives 3, and both radii -1. Accept `"unlimited"` only for radius fields, mapping it to -1 as the current CLI does; match existing query-limit ranges. Use checked integer conversions. Build-type verbosity follows current routed behavior. Do not call `EngineConfig::IsValid` in the common library: it also validates serving files and lives in the engine module.

  `makeEngineConfig` applies the profile algorithm and selected loading mode, keeping the storage path empty for shared memory. `validateConfig` checks stage-specific required files without opening serving artifacts for shared-memory startup. `validatePreparedData` reads the descriptor and checks common and algorithm-specific artifacts (`.hsgr` for CH; `.partition`, `.cells`, `.cell_metrics`, `.mldgr` for MLD), using storage facilities rather than equating common-file completeness with algorithm availability. Actual engines still validate data on load.

- [ ] **Step 4: Verify.** Build and run the full `deployment-tests` executable. Confirm existing `storage-tests` still passes. Neither configuration parsing nor descriptor reading writes output directories or publishes shared memory.
- [ ] **Step 5: Commit.** Suggested message: `feat: add shared deployment configuration`.

### Task 3: Prepare all configured profiles safely

**Files:**
- Create: `include/deployment/prepare.hpp`, `src/deployment/prepare.cpp`, `include/deployment/output_directory.hpp`, `src/deployment/output_directory.cpp`, `src/tools/prepare.cpp`.
- Create: `unit_tests/deployment/output_directory.cpp`, `features/options/prepare.feature`, `features/support/multiple_profiles.js`, `features/step_definitions/multiple_profiles.js`.
- Modify: `CMakeLists.txt`, `unit_tests/CMakeLists.txt`.

**Interfaces:**
- Consumes: Task 2 configuration, `datasetBase`, validation, and descriptor writer.
- Produces: `int osrm::deployment::prepare(const DeploymentConfig &config, std::string &error);` and installed command `osrm-prepare --config <path> [--threads <positive integer>] [--verbosity <level>]`.
- Output ownership: a move-only RAII `OutputDirectory` in `osrm::deployment`, acquired by `static std::optional<OutputDirectory> acquire(const std::filesystem::path &path, std::string &error);`. It owns an OS-released exclusive lock through completion publication. Lock files live beside the output directory; persistent lock-file existence alone must not mean the lock is held. Canonicalize existing parent components before choosing the lock identity.

- [ ] **Step 1: Write command and directory tests.** Using the existing car and foot Lua files, generate a small OSM input and a configuration with CH and MLD entries. Assert success creates both artifact sets and `prepared.json`; a second attempt exits nonzero without changing them. A missing second Lua file must fail preflight before extraction. A Lua file that parses but fails during processing must leave no descriptor and identify its profile and stage. Use a temporary test Lua file outside `profiles/`, not a product profile change.

  ```gherkin
  Scenario: Failed preparation cannot be served as a completed release
    Given a deployment configuration with a profile that fails during extraction
    When I prepare the configured profiles
    Then preparation should fail with the profile name and stage
    And the deployment completion descriptor should not exist
  ```

  Add lock contention and release-after-owner-exit tests. Where the filesystem is case-insensitive, profiles `car` and `Car` must fail before any stage starts. Create every profile directory exclusively during preflight, detecting filesystem aliases and reserved names. Do not globally lowercase public profile names. Use direct argv arrays in helpers so paths containing spaces remain intact.

- [ ] **Step 2: Run the tests failing.** Run `deployment-tests` directory cases and `OSRM_BUILD_DIR="${BUILD_DIR}" npx cucumber-js -p home -p mld -p mmap features/options/prepare.feature`. Command scenarios should initially fail because `osrm-prepare` is unavailable.
- [ ] **Step 3: Implement the executable and orchestration.** Link `osrm_preparation` to `osrm_deployment`, `osrm_extract`, `osrm_partition`, `osrm_contract`, and `osrm_customize`; link the new main to that library and Program_options. Configure stage input/output names with existing `UseDefaultOutputNames` conventions. Execute extract/contract for CH and extract/partition/customize for MLD, one profile at a time. Pass the same positive preparation thread budget into every stage.

  Check stage exit status: all four public preparation wrappers currently return void and discard the underlying integer results. Call `Extractor(config).run(scripting_environment)`, `Contractor(config).Run()`, `Partitioner().Run(config)`, and `Customizer().Run(config)` directly, retaining each result. Construct the scripting environment exactly as the existing `osrm::extract` wrapper does, including driving-side mode and location-dependent data. Convert IO failures to a nonzero command result containing profile and stage. Publish `prepared.json` only after all artifacts validate. Since `validatePreparedData` requires the descriptor, share its artifact-checking logic privately rather than calling it before the descriptor exists. Write a temporary descriptor and rename it while holding the output lock. Never delete a caller's nonempty output directory.

  Add CMake build and install rules, Lua symbol exports matching the existing Lua-hosting executables, and test target dependencies. Command help/version must work without reading configuration. The cucumber helpers own and terminate only their scenario's processes and directories.

- [ ] **Step 4: Verify.** Build `osrm-prepare` and `deployment-tests`; run directory tests and the complete prepare feature. Confirm the produced CH and MLD datasets each load through legacy `osrm-routed --trial` with the matching algorithm. Confirm stage failures cannot produce a success exit code or descriptor.
- [ ] **Step 5: Commit.** Suggested message: `feat: prepare configured routing profiles`.

### Task 4: Dispatch requests to configured engines

**Files:**
- Create: `include/server/multi_profile_service_handler.hpp`, `src/server/multi_profile_service_handler.cpp`, `unit_tests/server/multi_profile_service_handler.cpp`, `features/options/routed/multiple_profiles.feature`.
- Modify: `src/tools/routed.cpp`, `unit_tests/server/request_handler.cpp`, `features/support/multiple_profiles.js`, `features/step_definitions/multiple_profiles.js`, `CMakeLists.txt`.

**Interfaces:**
- Consumes: Task 1 isolation and Task 2 `ServerOptions`, `makeEngineConfig`, `validatePreparedData`.
- Produces: `server::MultiProfileServiceHandler final : public ServiceHandlerInterface`, constructed with `std::unordered_map<std::string, std::unique_ptr<ServiceHandlerInterface>> handlers`; implements the two existing `RunQuery` overloads unchanged. Production children are existing `ServiceHandler` instances; tests can supply recording handlers.

- [ ] **Step 1: Add dispatch and HTTP tests.** For GET and POST, assert that the selected child receives the original service, version, query, body, and prefix length, and that the other child is untouched. Unknown profiles return `Status::Error` and JSON `code == "InvalidProfile"`; a request-handler test asserts HTTP 400. Test HEAD and OPTIONS through existing HTTP behavior, without treating OPTIONS as an engine query.
- [ ] **Step 2: Add startup and configuration scenarios and run them failing.** Prepare a car/foot release through Task 3. Assert mixed CH/MLD startup and all-or-nothing initialization; an incomplete descriptor, missing artifact, or algorithm mismatch must prevent readiness. Include `--trial`, configuration-relative paths, a serving-only configuration lacking OSM/Lua files, and a single-profile legacy invocation with an arbitrary URL label.

  Pin precedence with `server.port=5101`, `server.max_table_size=123`, and `server.max_header_size=0`: omitted CLI settings preserve 5101 and 123; explicit `--port 5102 --max-table-size 456` applies 5102 and 456; automatic header sizing uses the final limit. Reject positional datasets and explicit single-engine options alongside `--config`. Run `server-tests` and the new routed feature; expect missing handler/CLI support to fail.
- [ ] **Step 3: Implement registry creation and CLI integration.** Load configuration, overlay only explicitly supplied server-wide options using Program_options' `defaulted()` state, then derive automatic header/body sizes. Validate the descriptor only for file-backed modes. Construct and label every child engine before starting the HTTP server; insert no fallback entry. Build the registry once and share it through the existing request handler and worker pool. Keep legacy startup in its existing path. In configuration mode, `--trial` initializes engines without entering the request loop, and never signals running readiness.
- [ ] **Step 4: Verify.** Build `osrm-routed` and `server-tests`; run the full unit executable and `OSRM_BUILD_DIR="${BUILD_DIR}" npx cucumber-js -p home -p mld -p mmap features/options/routed/multiple_profiles.feature`. Exercise route, nearest, table, match, trip, and tile against per-profile standalone baselines; include POST for the services already supporting it. Run the complete existing `features/options/routed` group to protect CLI compatibility.
- [ ] **Step 5: Commit.** Suggested message: `feat: serve multiple profiles in osrm-routed`.

### Task 5: Load and refresh named datasets from the configuration

**Files:**
- Create: `include/deployment/store.hpp`, `src/deployment/store.cpp`, `features/options/datastore/multiple_profiles.feature`.
- Modify: `src/tools/store.cpp`, `include/engine/data_watchdog.hpp`, `unit_tests/library/multiple_datasets.cpp`, `features/support/multiple_profiles.js`, `features/step_definitions/multiple_profiles.js`, `CMakeLists.txt`.

**Interfaces:**
- Consumes: Task 2 configuration, descriptor validation, dataset names, and `storage::Storage::Run(int max_wait, const std::string &name, bool only_metric)`.
- Produces: `int osrm::deployment::publishProfiles(const DeploymentConfig &config, int max_wait, std::string &error);` and `osrm-datastore --config <path> [--max-wait <value>] [--verbosity <level>]`.
- Config-mode publication always uses `only_metric=false`. Existing single-dataset commands retain targeted metric-update support. Reject combining `--config` with positional data, `--dataset-name`, `--only-metric`, listing, lock removal, or spring cleaning. Explicit feature-dataset and verbosity overrides apply globally.

- [ ] **Step 1: Write real datastore scenarios and run them failing.** In an isolated test namespace, publish two profiles, start one configured server, and query both. Hide the preparation directory and ensure a newly started shared-memory server still works. Restore it, prepare another release, publish it, and assert requests eventually observe the new profile data while the other profile remains correct. Poll a concrete routing result or update signal with a deadline; never assume a fixed sleep establishes publication.

  Add single-profile `--only-metric` refresh with unchanged node/edge counts to exercise Task 1's cache invalidation, plus a controlled test retaining an old facade while a new facade becomes available. Old and new requests must each use one coherent version. Test failure during publication of a later profile: previous successful publications remain usable, the exit code is nonzero, and logs identify both the successful entries and failing profile. Use a test seam or controlled storage error to exercise this rather than exhausting host memory.
- [ ] **Step 2: Implement publication and error reporting.** Validate all prepared inputs before publishing; then call `Storage::Run` sequentially in configuration order with derived names, reporting each success and stopping at the first runtime failure. Add dataset names to watchdog refresh and shutdown logs. Retain existing storage capacity enforcement and its synchronization. Do not clear global shared memory or implement cross-profile rollback. Document that publishing an algorithm change requires stopping/restarting affected consumers; ordinary live refreshes preserve algorithms and registry names.
- [ ] **Step 3: Verify.** Build datastore and routed; run `OSRM_BUILD_DIR="${BUILD_DIR}" npx cucumber-js -p home -p mld -p mmap features/options/datastore/multiple_profiles.feature` and the existing `features/options/datastore/datastore.feature`. The new scenarios explicitly control their storage mode and carry isolation tags. Also run `storage-tests`, plus the cache regressions from Task 1 after an actual metric refresh.
- [ ] **Step 4: Commit.** Suggested message: `feat: publish configured profiles to shared memory`.

### Task 6: Package the workflow and validate deployment behavior

**Files:**
- Modify: `src/python/CMakeLists.txt`, `src/python/osrm/__main__.py`, `src/nodejs/CMakeLists.txt`, `docker/Dockerfile-debian`, relevant binary packaging rules in `CMakeLists.txt` and `cmake/CPackConfig.cmake`, and release workflows only where they enumerate executable names.
- Modify: `docs/profiles.md`, `docs/tools.md`, `docs/http.md`, `docs/routed.md`, `docs/python/development.md`.
- Create: `example/multiple-profiles/osrm.json`, `test/python/test_main.py`.
- Extend: the multi-profile cucumber features and helpers from Tasks 3–5; use a focused benchmark driver under `scripts/` only if existing tools cannot perform the required alternating-profile workload.

**Interfaces:**
- Installed executable: `osrm-prepare`, also exposed as `python -m osrm prepare` in wheels shipping the executable.
- Runnable example: car and foot Lua paths relative to `example/multiple-profiles/osrm.json`, input `../../test/data/monaco.osm.pbf`, and fresh output `../../test/data/multiple-profiles-example`. Explain changing the output directory for subsequent runs.

- [ ] **Step 1: Add packaging checks and operator documentation alongside the packaging change.** Check Python dispatcher help and subprocess argument forwarding for `prepare`. Install the binary into `osrm/bin/` with the other Python executables, and ensure Node/binary packages that contain preparation tools include it. Add `osrm-prepare --help` to the container smoke checks. Document the entire schema with exact defaults and conflicts, `InvalidProfile`, the two-command file workflow, the three-command shared-memory workflow, incomplete-release recovery, non-atomic multi-profile updates, and expected cumulative dataset memory/disk usage. The executable paths in the example must match the selected installed layout; bus examples require a supplied Lua file.
- [ ] **Step 2: Run the final behavior matrix.** Expand the existing multi-profile features to cover mmap, direct memory, and shared memory with CH/CH, MLD/MLD, and CH/MLD entries. Compare all six services to standalone engines, alternating requests on one worker and using concurrent requests on several workers. Preserve feature availability and methods of each existing service. Run the full changed feature files and impacted unit targets, then `OSRM_BUILD_DIR="${BUILD_DIR}" npm test` once for compatibility. Add these features to CI using existing profile/loading configurations; do not duplicate a matrix already exercised inside a scenario. All scenarios and unit assertions must pass without adding `@todo` tags.
- [ ] **Step 3: Verify packaged entry points.** Set `PACKAGE_CHECK_DIR` to a new temporary directory, run `cmake --install "${BUILD_DIR}" --prefix "${PACKAGE_CHECK_DIR}"`, and invoke the installed preparation/server help and the documented example. Run `pytest test/python/test_main.py` and `python -m osrm prepare --help` in the repository's configured editable/wheel environment; both must succeed and the command tests must preserve subprocess arguments and exit status. Check container/package smoke jobs on their supported platforms, with explicit attention to Lua module symbol exports and Windows output locking. Do not report unrun platform checks as passing.
- [ ] **Step 4: Measure the deployment tradeoff.** Reuse the same prepared data and query sets for standalone servers and the combined server. Record single-profile latency/throughput, alternating-profile latency/throughput, and peak memory at fixed total worker counts. Exercise enough profile switches to check that memory plateaus. Keep the cache identity fix in both baselines so measurements isolate dispatch and switching costs. Record results and remaining limitations in the change description; investigate an unexplained unbounded growth or large regression before completion. No performance gain is an acceptance promise.
- [ ] **Step 5: Finish verification and commit.** Run formatting with clang-format-22, relevant JS/Python lint, and `git diff --check`; rerun affected tests if formatting or fixes alter behavior. Suggested message: `docs: ship and document multi-profile deployments`. Follow the repository PR template if a PR is subsequently requested, retain unchecked review, disclose the actual harness/model only in that PR description, and add no AI attribution to commits.

## Plan self-review

Configuration and compatibility requirements map to Tasks 2 and 4; preparation and completion publication to Task 3; cache lifetime to Task 1; shared-memory and partial-update behavior to Task 5; deployment documentation, package entry points, and cumulative validation to Task 6. Every Review Focus item has an owning test. Commands and interfaces are consistent across consumers, and the public preparation wrappers' discarded return values are handled explicitly.

The next decision is whether to execute this plan and, if so, whether implementation should stay with the primary agent or use a fresh implementer and reviewer for each task. The current session has authorized exploration and design; writing this plan does not itself start implementation.
