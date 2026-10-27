#!/usr/bin/env python3
"""Compare a prepared two-profile deployment at a fixed total of two workers."""

import argparse
import http.client
import json
import os
import re
import statistics
import subprocess
import tempfile
import threading
import time
from pathlib import Path


class Server:
    def __init__(self, binary, arguments):
        self.logs = []
        self.port = None
        self.ready = threading.Event()
        self.process = subprocess.Popen(
            [str(binary), *arguments, "--port", "0", "--verbosity", "INFO"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env={**os.environ, "DISABLE_ACCESS_LOGGING": "1"},
        )
        self.reader = threading.Thread(target=self.read_logs, daemon=True)
        self.reader.start()
        if not self.ready.wait(20):
            self.close()
            raise RuntimeError("Server failed to start: " + "".join(self.logs))
        self.connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)

    def read_logs(self):
        for line in self.process.stdout:
            self.logs.append(line)
            match = re.search(r"Listening on: .*:(\d+)", line)
            if match:
                self.port = int(match[1])
            if self.port and "running and waiting for requests" in line:
                self.ready.set()

    def request(self, profile, coordinates):
        for attempt in range(2):
            try:
                self.connection.request(
                    "GET", f"/route/v1/{profile}/{coordinates}?overview=full&generate_hints=false"
                )
                response = self.connection.getresponse()
                data = response.read()
                break
            except (http.client.RemoteDisconnected, ConnectionResetError, BrokenPipeError):
                self.connection.close()
                if attempt:
                    raise
        if response.status != 200:
            raise RuntimeError(data.decode())
        return data

    def close(self):
        if hasattr(self, "connection"):
            self.connection.close()
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.reader.join(timeout=1)


def measure(servers, selections, coordinates, requests):
    expected = {}
    for index in range(128):
        server_index, name = selections[index % len(selections)]
        expected[name] = servers[server_index].request(name, coordinates)
    elapsed = []
    memory = []
    start = time.perf_counter()
    for index in range(requests):
        server_index, name = selections[index % len(selections)]
        before = time.perf_counter_ns()
        actual = servers[server_index].request(name, coordinates)
        elapsed.append((time.perf_counter_ns() - before) / 1_000_000)
        if actual != expected[name]:
            raise RuntimeError(f"Routing changed for {name}")
        if index % max(1, requests // 20) == 0:
            rss = subprocess.check_output(
                ["ps", "-o", "rss=", "-p", ",".join(str(s.process.pid) for s in servers)],
                text=True,
            )
            memory.append(sum(int(value) for value in rss.split()))
    duration = time.perf_counter() - start
    elapsed.sort()
    return {
        "requests": requests,
        "requests_per_second": round(requests / duration, 1),
        "median_ms": round(statistics.median(elapsed), 3),
        "p95_ms": round(elapsed[int(len(elapsed) * 0.95)], 3),
        "peak_sampled_rss_kib": max(memory),
        "rss_samples_kib": memory,
    }, expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--coordinates", required=True)
    parser.add_argument("--requests", type=int, default=3000)
    args = parser.parse_args()
    if args.requests < 100:
        parser.error("use at least 100 requests")
    config = json.loads(args.config.read_text())
    if len(config["profiles"]) != 2:
        parser.error("configuration must contain exactly two profiles")
    output = (args.config.resolve().parent / config["output_directory"]).resolve()
    profiles = config["profiles"]
    binary = args.build.resolve() / "osrm-routed"
    combined_config = {
        "version": 1,
        "output_directory": str(output),
        "profiles": [{"name": p["name"], "algorithm": p["algorithm"]} for p in profiles],
        "server": {"threads": 2, "storage": "mmap", "ip": "127.0.0.1"},
    }
    results = {"total_workers": 2, "coordinates": args.coordinates, "storage": "mmap"}
    answers = {}
    with tempfile.TemporaryDirectory(prefix="osrm-benchmark-") as directory:
        path = Path(directory) / "config.json"
        path.write_text(json.dumps(combined_config))
        for mode in [
            "single_standalone",
            "single_combined",
            "alternating_standalone",
            "alternating_combined",
        ]:
            servers = []
            try:
                if mode.endswith("combined"):
                    servers.append(Server(binary, ["--config", str(path)]))
                else:
                    count = 2 if mode.startswith("alternating") else 1
                    for profile in profiles[:count]:
                        servers.append(
                            Server(
                                binary,
                                [
                                    str(output / profile["name"] / "map.osrm"),
                                    "--algorithm",
                                    profile["algorithm"],
                                    "--mmap",
                                    "--threads",
                                    str(2 // count),
                                ],
                            )
                        )
                selections = [(0, profiles[0]["name"])]
                if mode.startswith("alternating"):
                    selections.append((0 if mode.endswith("combined") else 1, profiles[1]["name"]))
                result, expected = measure(servers, selections, args.coordinates, args.requests)
                for name, answer in expected.items():
                    if name in answers and answers[name] != answer:
                        raise RuntimeError(f"Standalone/combined mismatch for {name}")
                    answers[name] = answer
                results[mode] = result
            finally:
                for server in servers:
                    server.close()
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
