# Results — 8 October 2026

Ryzen 7 3700X · Linux · Release (`-O3`, C++20) · 16-byte payload · five repetitions
per case. All **480 measured runs** passed sequence verification; all 30 correctness
tests passed before the matrix. This was one session on a desktop with dynamic
CPU frequency and normal background activity.

## Main comparison

Separate physical cores (CPUs 0/1, sharing L3), requested capacity 1,024.
Throughput values are median **million transfers/second** from throughput mode;
p99 is the median of per-run percentiles from the separate latency mode.

| Queue | Continuous throughput | Slow-consumer throughput | Slow-consumer p99 (µs) |
|---|---:|---:|---:|
| Custom | 99.0 | 6.98 | 181.0 |
| Boost | 102.4 | 6.97 | 184.8 |
| Rigtorp | 278.5 | 7.12 | 179.2 |
| Moodycamel | 262.8 | 7.34 | 309.8 |

Continuous traffic exposes substantial throughput differences. With consumer work
added, the rates converge, consistent with the consumer computation becoming the
bottleneck. Bursty throughput was around 0.63 million/s for all four, largely
constrained by the producer's deliberate pauses.

## Other observations

- **Capacity:** custom slow-consumer p99 rose from 13 µs to 181 µs to 2,864 µs as
  capacity increased from 64 to 1,024 to 16,384. This is consistent with a larger
  backlog behind the consumer; a larger buffer does not automatically lower latency.
- **Placement:** custom continuous throughput was 126 million/s on sibling hardware
  threads versus 99 million/s on separate cores. In latency mode, p99 was higher
  on siblings (30.4 versus 1.2 µs). Placement affected the two measurements differently.
- **Interpretation:** low continuous-traffic latency can reflect little queue buildup.
  It does not establish faster isolated queue operations. Occupancy was not recorded,
  so the precise cause remains a hypothesis to investigate.

## Limitations

- Workers retry immediately and yield on every 64th cumulative failure; success
  does not reset the count. Different failure rates mean different retry/yield costs.
  Alternative retry policies were not tested.
- Latency starts before the first enqueue attempt and ends after dequeue. It includes
  waiting for space and time in the queue, but excludes that item's later consumer
  work. Timestamping and verification add overhead.
- Equal requested capacities need not give equal usable storage: Moodycamel may
  reserve more, affecting backpressure comparisons.
- Warmup uses separate queues/workers. Queue order within each invocation is fixed;
  case order is shuffled. Five repetitions on one machine support observations
  under these conditions, rather than a universal ranking.

## Full data and rerunning

[Throughput summary CSV](throughput_summary.csv) ·
[Latency summary CSV](latency_summary.csv) · [Raw per-run CSVs](raw/)

The `source` paths in the summaries point to the original per-run CSVs in `raw/`.
Metric columns use seconds, items/second, and nanoseconds as named in their headers;
the overview above converts rates to millions/second and latency to microseconds.

The summaries include baseline, consumer, and bursty workloads; throughput and
latency modes; unpinned, sibling, and separate-core placement; and selected capacity
comparisons. Ranges are observed minimum–maximum, not confidence intervals. Latency
summaries aggregate per-run percentiles, not pooled raw latency samples.

After building Release, run `python3 tools/run_benchmark_matrix.py` from the
repository root. Use `--dry-run` to inspect the configuration and `--repetitions 10`
for more repetitions. The [commented runner](../../tools/run_benchmark_matrix.py)
launches the existing C++ executable and summarizes its CSVs.

Commands, machine/build information, and source version are recorded in
[metadata](details/metadata.json). The summaries were reformatted from the saved
CSVs; the measurements have not been rerun or changed.
