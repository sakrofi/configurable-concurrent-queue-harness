#include "harness/benchmark_harness.hpp"
#include "queues/implementations/boost_spsc_adaptor.hpp"
#include "queues/implementations/moody_spsc_adaptor.hpp"
#include "queues/implementations/rigtorp_spsc_adaptor.hpp"
#include "queues/implementations/spsc_v1.hpp"

#include <fstream>
#include <iomanip>
#include <locale>

namespace lab::harness {
void validate(const BenchmarkConfig& config, const Workload& workload) {
    if (!config.capacity || !config.items || !config.repetitions || !config.timeout_seconds) {
        throw std::invalid_argument("Capacity, items, repetitions and timeout must be positive");
    }
    workload_name(workload.type); // Reject an invalid enum before starting workers.
    if (workload.type == WorkloadType::Bursty && !workload.burst_size) {
        throw std::invalid_argument("Burst size must be positive");
    }
}

// Fixed integer computation
std::uint64_t consumer_work(std::uint64_t state, std::size_t iterations) {
    for (std::size_t i = 0; i < iterations; ++i) {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        state *= 2685821657736338717ULL;
    }
    return state;
}

namespace {
// A normal queue type parameter; no template-template parameter or mode dispatcher.
template<lab::queues::SpscQueue Queue>
void append_runs(std::vector<RunResult>& results, const char* name,
                 const BenchmarkConfig& config, const std::vector<Workload>& workloads,
                 const std::vector<MeasurementMode>& modes, CpuPair cpus) {
    for (const auto& workload : workloads) {
        for (auto mode : modes) {
            if (config.warmup_items) {
                auto warmup = config;
                warmup.items = config.warmup_items;
                run_one_measurement<Queue>(name, warmup, workload, mode, cpus, 0); // Discard this result.
            }
            for (std::size_t repetition = 1; repetition <= config.repetitions; ++repetition) {
                results.push_back(run_one_measurement<Queue>(name, config, workload, mode, cpus, repetition));
            }
        }
    }
}

// CSV quotes are doubled so even a name containing a comma or quote is readable by Python.
std::string csv_string(const std::string& value) {
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') out += '"';
        out += ch;
    }
    return out + '"';
}
}

std::vector<RunResult> run_benchmarks(const BenchmarkConfig& config,
                                     const std::vector<Workload>& workloads,
                                     const std::vector<MeasurementMode>& modes) {
    if (workloads.empty() || modes.empty()) throw std::invalid_argument("No runs selected");
    for (const auto& workload : workloads) validate(config, workload);
    const auto cpus = select_cpus(config.affinity);
    std::vector<RunResult> results;
    append_runs<boost_spsc_adaptor<Payload>>(results, "boost", config, workloads, modes, cpus);
    append_runs<moody_spsc_adaptor<Payload>>(results, "moodycamel", config, workloads, modes, cpus);
    append_runs<rigtorp_spsc_adaptor<Payload>>(results, "rigtorp", config, workloads, modes, cpus);
    append_runs<spsc_v1<Payload>>(results, "custom", config, workloads, modes, cpus);
    return results;
}

// Write one row per measured run, after timing and verification are finished.
void export_csv(const std::vector<RunResult>& results, const std::filesystem::path& prefix) {
    if (!prefix.parent_path().empty()) std::filesystem::create_directories(prefix.parent_path());
    std::ofstream csv(prefix.string() + ".csv");
    if (!csv) throw std::runtime_error("Cannot open CSV output file");
    csv.imbue(std::locale::classic()); // Always use a decimal point, not a locale's decimal comma.
    csv << std::setprecision(17);
    csv << "queue,workload,mode,repetition,requested_capacity,items,warmup_items,affinity,"
           "producer_cpu,consumer_cpu,consumer_iterations,burst_size,burst_gap_us,"
           "elapsed_seconds,items_per_second,failed_pushes,failed_pops,latency_samples,"
           "p50_ns,p99_ns,p999_ns,consumer_work_result,verified,build_type\n";
    for (const auto& r : results) {
        const char* mode = r.mode == MeasurementMode::Latency ? "latency" : "throughput";
        csv << csv_string(r.queue) << ',' << workload_name(r.workload.type) << ',' << mode
            << ',' << r.repetition << ',' << r.config.capacity << ',' << r.config.items
            << ',' << r.config.warmup_items << ',' << affinity_name(r.config.affinity)
            << ',' << r.cpus.producer << ',' << r.cpus.consumer
            << ',' << r.workload.consumer_iterations << ',' << r.workload.burst_size
            << ',' << r.workload.burst_gap_us << ',' << r.seconds << ',' << r.items_per_second
            << ',' << r.failed_pushes << ',' << r.failed_pops << ',';
        if (r.latency) {
            csv << r.latency->samples << ',' << r.latency->p50_ns << ','
                << r.latency->p99_ns << ',' << r.latency->p999_ns;
        } else csv << "0,,,"; // No latency samples or percentiles in throughput mode.
        csv << ',' << r.consumer_work_result << ",true," << csv_string(LAB_BUILD_TYPE) << '\n';
    }
    csv.close();
    if (!csv) throw std::runtime_error("Failed to write CSV output file");
}
} // namespace lab::harness
