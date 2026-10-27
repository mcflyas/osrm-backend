#!/usr/bin/env python3
"""Compare native standalone and combined OSRM table serving on saved requests."""

import argparse
import hashlib
import http.client
import json
import math
import os
import platform
import statistics
import subprocess
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from benchmark_multiple_profiles import Server


class MemorySampler:
    def __init__(self, servers):
        self.pids = ",".join(str(server.process.pid) for server in servers)
        self.samples = []
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.sample, daemon=True)
        self.thread.start()

    def sample(self):
        while not self.stop.is_set():
            result = subprocess.run(
                ["ps", "-o", "rss=", "-p", self.pids],
                text=True,
                capture_output=True,
                check=False,
            )
            if result.returncode == 0:
                self.samples.append(sum(int(value) for value in result.stdout.split()))
            self.stop.wait(0.2)

    def finish(self):
        self.stop.set()
        self.thread.join()
        if not self.samples:
            raise RuntimeError("No server RSS samples were collected")
        return {
            "peak_sampled_rss_kib": max(self.samples),
            "last_sampled_rss_kib": self.samples[-1],
            "sample_count": len(self.samples),
        }


class TableClient:
    def __init__(self, server, profile, bodies, expected):
        self.connection = http.client.HTTPConnection("127.0.0.1", server.port, timeout=180)
        self.profile = profile
        self.bodies = bodies
        self.expected = expected

    def request(self, size):
        started = time.perf_counter()
        for attempt in range(2):
            try:
                self.connection.request(
                    "POST",
                    f"/table/v1/{self.profile}",
                    self.bodies[size],
                    {"Content-Type": "application/json"},
                )
                response = self.connection.getresponse()
                break
            except (http.client.RemoteDisconnected, ConnectionResetError, BrokenPipeError):
                self.connection.close()
                if attempt:
                    raise
        first_byte = time.perf_counter() - started
        if response.status != 200:
            raise RuntimeError(f"HTTP {response.status}: {response.read(4096)!r}")
        digest = hashlib.sha256()
        response_bytes = 0
        null_cells = 0
        tail = b""
        while chunk := response.read(1024 * 1024):
            digest.update(chunk)
            response_bytes += len(chunk)
            null_cells += (tail + chunk).count(b"null")
            tail = chunk[-3:]
        elapsed = time.perf_counter() - started
        signature = (digest.hexdigest(), response_bytes, null_cells)
        key = (self.profile, size)
        if key in self.expected and self.expected[key] != signature:
            raise RuntimeError(f"Response mismatch for {self.profile}, {size} locations")
        self.expected[key] = signature
        return {
            "seconds": elapsed,
            "first_byte_seconds": first_byte,
            "response_bytes": response_bytes,
            "null_cells_both_annotations": null_cells,
            "sha256": signature[0],
        }

    def close(self):
        self.connection.close()


def summarize(rows):
    values = sorted(row["seconds"] for row in rows)
    return {
        "requests": len(values),
        "median_seconds": statistics.median(values),
        "min_seconds": values[0],
        "max_seconds": values[-1],
        "sample_p95_seconds": values[math.ceil(len(values) * 0.95) - 1],
        "median_first_byte_seconds": statistics.median(row["first_byte_seconds"] for row in rows),
    }


def order(index):
    return ("standalone", "combined") if index % 2 == 0 else ("combined", "standalone")


def concurrently(big, small, largest, smallest):
    barrier = threading.Barrier(2)
    done = threading.Event()

    def large_requests():
        barrier.wait()
        try:
            return [big.request(largest) for _ in range(2)]
        finally:
            done.set()

    def small_requests():
        rows = []
        barrier.wait()
        while not done.is_set():
            rows.append(small.request(smallest))
        return rows

    started = time.perf_counter()
    with ThreadPoolExecutor(max_workers=2) as executor:
        large_future = executor.submit(large_requests)
        small_future = executor.submit(small_requests)
        large_rows, small_rows = large_future.result(), small_future.result()
    return {
        "elapsed_seconds": time.perf_counter() - started,
        "large": large_rows,
        "small": small_rows,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--workload", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--mixed-rounds", type=int, default=3)
    args = parser.parse_args()
    if args.samples < 2 or args.mixed_rounds < 1 or args.workers < 2 or args.workers % 2:
        parser.error("samples >= 2, mixed-rounds >= 1 and a positive even worker count are required")
    if args.output.exists():
        parser.error("output file must not exist")
    config = json.loads(args.config.read_text())
    profiles = config["profiles"]
    if len(profiles) != 2:
        parser.error("configuration must contain exactly two profiles")
    primary, secondary = (profile["name"] for profile in profiles)
    data = (args.config.resolve().parent / config["output_directory"]).resolve()
    manifest = json.loads((args.workload / "manifest.json").read_text())
    bodies = {}
    for case in manifest["cases"]:
        body = (args.workload / case["file"]).read_bytes()
        if hashlib.sha256(body).hexdigest() != case["sha256"]:
            raise RuntimeError(f"Workload hash mismatch: {case['file']}")
        bodies[case["locations"]] = body
    sizes = sorted(bodies)
    smallest, largest = sizes[0], sizes[-1]
    binary = args.build.resolve() / "osrm-routed"
    results = {
        "completed": False,
        "platform": platform.platform(),
        "binary_version": subprocess.check_output([str(binary), "--version"], text=True).strip(),
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "total_workers": args.workers,
        "storage": "mmap",
        "profiles": [{"name": p["name"], "algorithm": p["algorithm"]} for p in profiles],
        "samples_per_size": args.samples,
        "workload": manifest,
        "timing": "POST HTTP through complete response; streaming SHA-256; no client JSON decoding",
        "load_samples": [],
        "phases": {},
    }
    expected = {}

    def checkpoint():
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(results, indent=2) + "\n")

    def load_sample(label):
        results["load_samples"].append({"label": label, "load_average": os.getloadavg()})

    def run_phase(name, active_profiles, workers_per_standalone, combined_config):
        servers = []
        clients = {}
        samplers = {}
        phase = {"results": {}, "memory": {}}
        try:
            standalone = {}
            for profile in profiles:
                if profile["name"] not in active_profiles:
                    continue
                server = Server(
                    binary,
                    [
                        str(data / profile["name"] / "map.osrm"),
                        "--algorithm",
                        profile["algorithm"],
                        "--mmap",
                        "--threads",
                        str(workers_per_standalone),
                        "--max-table-size",
                        str(largest),
                        "--ip",
                        "127.0.0.1",
                    ],
                )
                servers.append(server)
                standalone[profile["name"]] = server
            combined = Server(binary, ["--config", str(combined_config)])
            servers.append(combined)
            for mode, mode_servers in [
                ("standalone", list(standalone.values())),
                ("combined", [combined]),
            ]:
                samplers[mode] = MemorySampler(mode_servers)
                for profile in active_profiles:
                    clients[mode, profile] = TableClient(
                        standalone[profile] if mode == "standalone" else combined,
                        profile,
                        bodies,
                        expected,
                    )
            print(f"Started {name}", flush=True)
            if name != "concurrent_profiles":
                for size in sizes:
                    rows = {mode: [] for mode in order(0)}
                    for mode in order(0):
                        clients[mode, primary].request(size)
                        if secondary in active_profiles:
                            clients[mode, secondary].request(smallest)
                    for repetition in range(args.samples):
                        load_sample(f"{name}:{size}:{repetition}")
                        for mode in order(repetition):
                            secondary_row = None
                            if secondary in active_profiles:
                                secondary_row = clients[mode, secondary].request(smallest)
                            rows[mode].append(
                                {
                                    "primary": clients[mode, primary].request(size),
                                    "secondary": secondary_row,
                                }
                            )
                    phase["results"][str(size)] = {
                        mode: {"summary": summarize([r["primary"] for r in values]), "samples": values}
                        for mode, values in rows.items()
                    }
                    print(
                        name,
                        size,
                        {
                            m: round(v["summary"]["median_seconds"], 3)
                            for m, v in phase["results"][str(size)].items()
                        },
                        flush=True,
                    )
                    results["phases"][name] = phase
                    checkpoint()
            else:
                for mode in order(0):
                    clients[mode, primary].request(largest)
                    clients[mode, secondary].request(smallest)
                batches = {mode: [] for mode in order(0)}
                for repetition in range(args.mixed_rounds):
                    for mode in order(repetition):
                        load_sample(f"{name}:{mode}:{repetition}")
                        batch = concurrently(
                            clients[mode, primary], clients[mode, secondary], largest, smallest
                        )
                        batches[mode].append(batch)
                        print(name, mode, repetition, "small requests:", len(batch["small"]), flush=True)
                for mode, values in batches.items():
                    large = [row for batch in values for row in batch["large"]]
                    small = [row for batch in values for row in batch["small"]]
                    elapsed = sum(batch["elapsed_seconds"] for batch in values)
                    phase["results"][mode] = {
                        "large_summary": summarize(large),
                        "small_summary": summarize(small),
                        "small_requests_per_second": len(small) / elapsed,
                        "batches": values,
                    }
            for mode, sampler in samplers.items():
                phase["memory"][mode] = sampler.finish()
            results["phases"][name] = phase
            checkpoint()
        finally:
            for sampler in samplers.values():
                if not sampler.stop.is_set():
                    sampler.finish()
            for client in clients.values():
                client.close()
            for server in reversed(servers):
                server.close()

    with tempfile.TemporaryDirectory(prefix="osrm-table-benchmark-") as temporary:
        path = Path(temporary) / "deployment.json"
        path.write_text(
            json.dumps(
                {
                    "version": 1,
                    "output_directory": str(data),
                    "profiles": results["profiles"],
                    "server": {
                        "threads": args.workers,
                        "storage": "mmap",
                        "ip": "127.0.0.1",
                        "max_table_size": largest,
                    },
                }
            )
        )
        run_phase("single_profile", [primary], args.workers, path)
        run_phase("alternating_profiles", [primary, secondary], args.workers // 2, path)
        run_phase("concurrent_profiles", [primary, secondary], args.workers // 2, path)
    results["completed"] = True
    results["finished_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    checkpoint()
    print(f"Completed: {args.output}", flush=True)


if __name__ == "__main__":
    main()
