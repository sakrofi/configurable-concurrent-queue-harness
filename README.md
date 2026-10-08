# Configurable Concurrent Queue Harness 

A C++20 learning project exploring single-producer/single-consumer queues. I built
an atomic ring buffer and a harness to compare it with Boost, Rigtorp, and
Moodycamel under continuous, slow-consumer, and bursty workloads.

The custom queue uses a spare slot to distinguish full from empty and acquire/release
ordering to publish items and release storage. Tests cover FIFO ordering, capacity,
wraparound, and concurrent transfers. The harness verifies every measured transfer
and measures throughput and p50/p99/p99.9 transfer latency across CPU placements.

## Results

On a Ryzen 7 3700X, the custom queue reached a median **99 million transfers/s**
under continuous traffic on separate cores (requested capacity 1,024). Rigtorp
reached 278 million/s; with simulated consumer work, all four were around 7 million/s.

[Results, limitations, and full measurements →](benchmark-results/2026-10-08-ryzen-3700x/RESULTS.md)

These are measurements of the queues under this harness's retry policy and tested
conditions. They illustrate how workload and placement affect the comparison.

## Build and test

Requires C++20, CMake 3.20+, Boost 1.70+ headers (`libboost-dev` on Debian/Ubuntu),
and Git/network access for initial dependency downloads.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 2
ctest --test-dir build-release --output-on-failure
```

## Run benchmarks

```sh
# Standard matrix: workloads, measurement modes, placements, and selected capacities
python3 tools/run_benchmark_matrix.py

# One focused comparison
./build-release/benchmarks --workload consumer --mode throughput \
  --items 10000000 --repetitions 5 --warmup 1000000 \
  --affinity cores --output results/consumer-cores

./build-release/benchmarks --help
```

The matrix requires Python 3, Linux, and available SMT sibling threads. It runs
480 measurements sequentially in about 7 minutes on this machine. Each new
`results/matrix-.../` directory contains:

- `raw/`: one CSV per configuration, retaining every measured repetition.
- `throughput_summary.csv` and `latency_summary.csv`: medians, observed min/max,
  run durations, and retries per item. Units are named in the columns.
- `metadata.json`: commands, machine/build details, source version, and run status.

Use `--dry-run` to inspect commands or `--summarize results/matrix-...` to recreate
summaries from saved raw CSVs. Build again after changing C++ source; the runner
uses the existing executable.

Benchmark data is exported only as CSV; `--output` chooses the prefix, not a format.
For individual runs, `--affinity none` works without CPU pinning. Reusing an
executable output prefix replaces its CSV; the matrix creates a fresh directory.
