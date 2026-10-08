#!/usr/bin/env python3
"""Run the C++ benchmark sequentially; save raw CSVs, CSV summaries and metadata.

Build Release first. Requires Python 3 and Linux with SMT siblings available.
Python chooses commands and summarizes repetitions; C++ does all queue timing.
"""

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shlex
import statistics
import subprocess
import time


# We Locate the repo root relative to this script.
ROOT = Path(__file__).resolve().parents[1]



# Starting counts but not guarantees of duration.
# The C++ benchmark may stop early if the measured transfers are reached.
# Workload, Mode, Measured Transfers, Discarded Warmup Transfers
PROFILES = [
    ("baseline", "throughput", 100_000_000, 5_000_000),
    ("baseline", "latency", 5_000_000, 500_000),
    ("consumer", "throughput", 10_000_000, 1_000_000),
    ("consumer", "latency", 5_000_000, 500_000),
    ("bursty", "throughput", 500_000, 50_000),
    ("bursty", "latency", 500_000, 50_000),
]

def capture(*args):
    """To Read optional context; unavailable diagnostic tools should not stop a run."""
    try:
        result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
        return result.stdout.strip() if result.returncode == 0 else None
    except OSError:
        return None


def summarize(output):
    """Summarize a single configuration per CSV file without altering them"""
    summaries = {"throughput": [], "latency": []}

    # New runs and the saved snapshot use raw/. Older runs have CSVs at the root.
    raw = output / "raw" if (output / "raw").is_dir() else output
    for path in sorted(raw.glob("*.csv")):
        if path.name in {"summary.csv", "throughput_summary.csv", "latency_summary.csv"}:
            continue
        with path.open() as stream:
            # DictReader uses the C++ exporter's column names as dictionary keys.
            # CSV values start as strings, so numeric fields are converted below.
            rows = list(csv.DictReader(stream))
        for queue in sorted({row["queue"] for row in rows}):
            group = [row for row in rows if row["queue"] == queue]
            first = group[0]
            summary = {key: first[key] for key in (
                "queue", "workload", "mode", "requested_capacity", "items",
                "warmup_items", "affinity", "producer_cpu", "consumer_cpu",
            )}
            if any(row[key] != value for row in group for key, value in summary.items()):
                raise ValueError(f"Mixed configurations in {path}; use one matrix case per CSV")
            summary.update(source=str(path.relative_to(output)), repetitions=len(group))

            # Aggregate the median, min, and max of each metric across repetitions.
            # This is not a pooled-sample statistic;
            # it simply computes the median of the per-repetition values.
            # For example, if the p99 latency values across repetitions are 10, 20, and 30 nanoseconds,
            # the median p99 would be reported as 20 nanoseconds.
            metrics = ["elapsed_seconds", "items_per_second"]
            if first["mode"] == "latency":
                metrics += ["p50_ns", "p99_ns", "p999_ns"]
            for metric in metrics:
                values = [float(row[metric]) for row in group]
                for stat, function in (("median", statistics.median), ("min", min), ("max", max)):
                    summary[f"{metric}_{stat}"] = function(values)
            for field in ("failed_pushes", "failed_pops"):
                # Normalize retry counts for different item counts. This can
                # exceed 1 because one item may require many failed attempts.
                summary[f"{field}_per_item_median"] = statistics.median(
                    int(row[field]) / int(row["items"]) for row in group
                )
            summary["all_verified"] = all(row["verified"] == "true" for row in group)
            summaries[first["mode"]].append(summary)
    # Separate modes avoid empty latency columns. Units remain seconds, items/s,
    # and nanoseconds, as named in the headers. min/max are the observed range.
    for mode, rows in summaries.items():
        if rows:
            with (output / f"{mode}_summary.csv").open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)


def build_context(binary):
    """Read a few build settings, without copying the entire CMake cache."""
    cache = binary.parent / "CMakeCache.txt"
    settings = {}
    if cache.is_file():
        for line in cache.read_text().splitlines():
            if line.startswith("CMAKE_") and "=" in line:
                key, value = line.split("=", 1)
                settings[key.split(":", 1)[0]] = value
    compiler = settings.get("CMAKE_CXX_COMPILER")
    return {
        "compiler": compiler,
        "compiler_version": capture(compiler, "--version") if compiler else None,
        "build_type": settings.get("CMAKE_BUILD_TYPE"),
        "cmake_cxx_flags": settings.get("CMAKE_CXX_FLAGS"),
        "cmake_release_flags": settings.get("CMAKE_CXX_FLAGS_RELEASE"),
    }


def main():
    # Control only the python script;
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build-release/benchmarks")
    parser.add_argument("--output", type=Path, help="New output directory; never overwrites an existing directory")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--seed", type=int, default=20261008)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--summarize", type=Path, help="Only regenerate summaries for an existing results directory")
    args = parser.parse_args()
    if args.summarize:
        # This path only reads existing results; it never launches benchmarks.
        summarize(args.summarize.resolve())
        return
    if args.repetitions < 1:
        parser.error("--repetitions must be positive")
    binary = args.binary.resolve()
    output = (args.output or ROOT / "results" / datetime.now(timezone.utc).strftime("matrix-%Y%m%dT%H%M%S.%fZ")).resolve()

    # Cartesian product: 3 affinity choices × 6 profiles = 18 command invocations.
    # C++ runs all four queues with given repetition count for each invocation
    cases = [(workload, mode, items, warmup, affinity, 1024)
             for affinity in ("cores", "siblings", "none")
             for workload, mode, items, warmup in PROFILES]

    # Focused capacity experiment: baseline in both modes, slow consumer in latency mode.
    # This adds 6 more cases, giving 24 total.
    cases += [(workload, mode, items, warmup, "cores", capacity)
              for capacity in (64, 16384)
              for workload, mode, items, warmup in PROFILES
              if workload == "baseline" or (workload == "consumer" and mode == "latency")]
    # This only shuffles the order of the cases, not the order of repetitions within a case.
    random.Random(args.seed).shuffle(cases)
    all_commands = []

    for workload, mode, items, warmup, affinity, capacity in cases:
        name = f"{workload}-{mode}-{affinity}-cap{capacity}"
        # Pass a list of arguments, not a shell command string: spaces in paths
        # stay inside their argument and there is no shell interpolation.
        command = [str(binary), "--workload", workload, "--mode", mode,
                   "--items", str(items), "--warmup", str(warmup),
                   "--repetitions", str(args.repetitions), "--affinity", affinity,
                   "--capacity", str(capacity), "--output", str(output / "raw" / name)]
        all_commands.append((name, command))
    print(f"{len(cases)} cases; {len(cases) * 4 * args.repetitions} measured runs; output: {output}", flush=True)
    if args.dry_run:
        # Inspection only: no new directory, metadata, or benchmark process.
        for _, command in all_commands:
            print(shlex.join(command))
        return
    if not binary.is_file():
        parser.error(f"Build the Release executable first: {binary}")
    if not hasattr(os, "sched_getaffinity"):
        parser.error("This matrix requires Linux CPU affinity; use the C++ CLI with --affinity none otherwise")

    # We refuse existing directories to avoid overwriting previous results from a previous
    # python run
    # must not be silently overwritten. The default name contains a UTC timestamp.
    output.mkdir(parents=True, exist_ok=False)
    (output / "raw").mkdir()

    # Source identity stays in metadata. Dirty=true records that local edits
    # existed, without saving their contents
    git_status = capture("git", "status", "--porcelain", "--untracked-files=normal")
    allowed = sorted(os.sched_getaffinity(0))
    cpu_info = Path("/proc/cpuinfo").read_text().splitlines()
    governors = [Path(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor") for cpu in allowed]
    metadata = {
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "binary": str(binary), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "git_commit": capture("git", "rev-parse", "HEAD"),
        "git_dirty": bool(git_status) if git_status is not None else None,
        "os": platform.platform(),
        "cpu_model": next((line.split(":", 1)[1].strip() for line in cpu_info if line.startswith("model name")), platform.machine()),
        "allowed_cpus": allowed,
        "cpu_governors": sorted({p.read_text().strip() for p in governors if p.exists()}),
        "build": build_context(binary),
        "seed": args.seed, "commands": [command for _, command in all_commands],
        "retry_policy": "yield every 64 cumulative failed attempts per worker",
        "queue_order": ["boost", "moodycamel", "rigtorp", "custom"],
        "status": "running", "completed_cases": 0,
    }
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    started = time.monotonic()

    # subprocess.run as we want to run sequentially not in parallel
    # ensures that the benchmark we don't have competing benchmark processes being measured
    # The C++ program exports its CSV after that case's measured runs finish.
    try:
        for index, (name, command) in enumerate(all_commands, 1):
            print(f"[{index}/{len(all_commands)}] {name} (elapsed {time.monotonic() - started:.0f}s)", flush=True)
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f"{name}: {result.stdout}{result.stderr}")
            with (output / "raw" / f"{name}.csv").open() as stream:
                rows = list(csv.DictReader(stream))
            # C++ verifies transfers; Python checks that all expected rows arrived.
            if len(rows) != 4 * args.repetitions or any(
                row["verified"] != "true" or row["build_type"] != "Release" for row in rows
            ):
                raise RuntimeError(f"Unexpected row count, verification failure, or non-Release build: {name}")
            metadata["completed_cases"] += 1
        summarize(output)
        metadata["status"] = "complete"
    except (OSError, ValueError, RuntimeError) as error:
        metadata["status"] = "failed"
        metadata["error"] = str(error)
        raise SystemExit(f"{error}\nCompleted raw CSVs remain in {output / 'raw'}") from error
    except KeyboardInterrupt:
        metadata["status"] = "interrupted"
        raise SystemExit("Interrupted; completed raw CSVs are preserved.")
    finally:
        metadata["finished_utc"] = datetime.now(timezone.utc).isoformat()
        metadata["wall_seconds"] = time.monotonic() - started
        (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Finished in {metadata['wall_seconds']:.0f}s. CSV summaries: {output}", flush=True)


if __name__ == "__main__":
    main()
