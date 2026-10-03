# ConcurrentLab


## What it does

- Implements a custom SPSC ring buffer
- Compares against Rigtorp, Boost and moodycamel queues
- Measures throughput and p99 transfer latency
- Tests continuous, bursty and slow-consumer workloads
- Supports configurable queue capacity and CPU placement
- Verifies that items are not lost, duplicated or reordered



## Why I built it

I wanted to understand how queue design, memory layout and producer/consumer behaviour affect latency and throughput in inter-thread communication.

## Benchmark scenarios

- Continuous traffic
- Bursty traffic
- Slower consumer / backpressure
- Different queue capacities
- Different CPU placements

## Correctness

Each run checks for:

- missing items
- duplicated items
- out-of-order items





## Queue implementations

| Queue | Implementation | Capacity behaviour |
| --- | --- | --- |
| Custom | `spsc_v1<T>` | Exactly the requested usable capacity; one additional spare slot |
| Boost | `boost::lockfree::spsc_queue<T>` | Exactly the requested usable capacity |
| Rigtorp | `rigtorp::SPSCQueue<T>` | Exactly the requested usable capacity with the current allocator |
| Moodycamel | `moodycamel::ReaderWriterQueue<T>` | Reserves at least the requested capacity; may provide more |






## Build and test

Requirements:

- A C++20 compiler and standard library supporting `std::jthread`, latches, and barriers.
- CMake 3.20 or newer.
- Boost 1.70 or newer development headers.
- Git and network access for the initial dependency downloads.

CMake fetches Rigtorp SPSCQueue, Moodycamel ReaderWriterQueue, and Catch2.
On Debian/Ubuntu, Boost headers are provided by `libboost-dev`.

The two queue dependencies are pinned to exact Git commits in `CMakeLists.txt`.
This keeps fresh builds from silently picking up changes from their `master`
branches. Update those commits deliberately and rerun the tests and benchmarks.
Compiler, Boost version, build flags, hardware, and operating-system differences
can still affect results; the pins alone do not make timings reproducible.



```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 2
ctest --test-dir build-release --output-on-failure
```

Use a Release build for performance measurements. A separate Debug build can be
created by changing the build directory and setting `CMAKE_BUILD_TYPE=Debug`.

Tests cover empty/full behaviour, FIFO ordering, capacity, wraparound, and storage
reuse. Concurrent tests transfer numbered items between a producer and consumer.
Harness tests cover statistics, sequence verification, cancellation, and affinity
setup. Passing tests provide evidence of correctness, not a proof of all possible
concurrent executions.

## Run benchmarks

```sh
# All queues, workloads, and measurement modes
./build-release/benchmarks --output results/spsc

# Longer baseline throughput runs on separate physical cores
./build-release/benchmarks --workload baseline --mode throughput \
  --items 1000000 --repetitions 5 --affinity cores --output results/baseline-cores

./build-release/benchmarks --help
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--items N` | `100000` | Successful transfers per run |
| `--capacity N` | `1024` | Requested queue capacity |
| `--repetitions N` | `3` | Measured runs per queue/workload/mode |
| `--warmup N` | `10000` | Transfers in a discarded warmup; `0` disables it |
| `--workload NAME` | `all` | `all`, `baseline`, `consumer`, or `bursty` |
| `--mode NAME` | `both` | `both`, `throughput`, or `latency` |
| `--affinity NAME` | `none` | `none`, `siblings`, or `cores` |
| `--output PREFIX` | `results/spsc` | Writes `PREFIX.csv`, replacing an existing file |

Defaults produce 72 measured runs: four queues × three workloads × two modes ×
three repetitions. A discarded warmup runs before each queue/workload/mode group.
Every run constructs a fresh queue, so warmup exercises code and CPU state rather
than preserving the same queue's contents or allocation.

## Workloads and affinity

| Workload | Producer | Consumer |
| --- | --- | --- |
| `baseline` | Continuous production | No simulated processing |
| `consumer` | Continuous production | 64 integer-mixing iterations per item |
| `bursty` | Bursts of 64 items with a requested 50 µs pause between bursts | No simulated processing |

Preset parameters are defined in `include/harness/workload_types.hpp`. Consumer
work is a fixed operation count, not a fixed duration. Its final integer-mixing
state is exported as `consumer_work_result` to keep the computation observable;
baseline and bursty workloads leave it at `1`. This CSV column was previously
named `consumer_checksum`; update analysis scripts that use the old header.
Sleep durations depend on scheduling and can exceed the requested pause.

Affinity selection uses CPUs allowed to the process:

- `none`: the operating system chooses placement.
- `siblings`: two logical CPUs on the same physical core.
- `cores`: different physical cores in the same package.

Pinned policies use Linux topology information and thread-affinity APIs. They
fail if no suitable pair is available. Other platforms support only `none`.
Separate cores in the same package do not necessarily share a last-level cache
or NUMA node. Selected CPU IDs are recorded in the output.

## Limitations

- Results include harness overhead from sequence verification, retry counting,
  and timestamp collection in latency mode.
- Workers yield every 64 failed queue attempts; this waiting policy affects results.
- Latency includes producer waiting for queue space, ending when the consumer
  receives the item.
- Warmup uses separate queues and threads. Queue implementations run in a fixed
  order, and CPU pinning does not eliminate system noise.
- Equal requested capacities may provide different usable capacities across queues.
  Results apply to the tested machine, build, and configuration.


## Repository layout

```text
benchmarks/main.cpp        Command-line entry point
harness/                   Run orchestration, CSV export, and Linux affinity
include/harness/           Configuration, measurement loop, statistics, verification
include/queues/            Common queue concept and queue implementations/adapters
tests/                     Queue and harness tests
```
