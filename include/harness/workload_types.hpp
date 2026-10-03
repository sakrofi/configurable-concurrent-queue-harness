#ifndef CONCURRENT_LAB_WORKLOAD_TYPES_HPP
#define CONCURRENT_LAB_WORKLOAD_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace lab::harness {
// Each enum lists only choices the program implements.
enum class WorkloadType { Baseline, ConsumerWork, Bursty };
enum class CpuAffinityPolicy { NoAffinity, Hyperthreads, TwoPhysicalCores };
enum class MeasurementMode { Throughput, Latency };

struct Workload {
    WorkloadType type = WorkloadType::Baseline;
    std::size_t consumer_iterations = 64; // Used only by ConsumerWork.
    std::size_t burst_size = 64;          // Used only by Bursty.
    std::uint64_t burst_gap_us = 50;      // Requested sleep between bursts.
};

struct BenchmarkConfig {
    std::size_t capacity = 1024;          // Requested; queues may round up.
    std::size_t items = 100000;           // Successful transfers per run.
    std::size_t warmup_items = 10000;     // 0 disables the discarded warmup.
    std::size_t repetitions = 3;
    unsigned timeout_seconds = 30;
    CpuAffinityPolicy affinity = CpuAffinityPolicy::NoAffinity;
};

// Keep the same payload size in both modes. Throughput leaves sent_ns at zero.
struct Payload {
    std::uint64_t sequence = 0;
    std::int64_t sent_ns = 0;
};

inline const char* workload_name(WorkloadType type) {
    switch (type) {
        case WorkloadType::Baseline: return "baseline";
        case WorkloadType::ConsumerWork: return "consumer";
        case WorkloadType::Bursty: return "bursty";
    }
    throw std::invalid_argument("Unknown workload");
}

inline std::vector<Workload> simple_workloads() {
    return {{WorkloadType::Baseline}, {WorkloadType::ConsumerWork}, {WorkloadType::Bursty}};
}
} // namespace lab::harness
#endif
