#include "harness/benchmark_harness.hpp"

#include <charconv>
#include <iostream>
#include <string_view>

namespace {
std::size_t checkNumber(std::string_view value) {
    std::size_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::invalid_argument("Expected a nonnegative integer: " + std::string(value));
    }
    return result;
}
}

int main(int argc, char** argv) {
    using namespace lab::harness;
    try {
        BenchmarkConfig config;
        auto workloads = simple_workloads();
        std::vector<MeasurementMode> modes{MeasurementMode::Throughput, MeasurementMode::Latency};
        std::filesystem::path output = "results/spsc";
        for (int i = 1; i < argc; ++i) {
            const std::string_view option = argv[i];
            if (option == "--help") {
                std::cout << "Usage: benchmarks [options]\n"
                    "  --items N             Transfers per run (100000)\n"
                    "  --capacity N          Requested capacity (1024)\n"
                    "  --repetitions N       Runs per queue/workload/mode (3)\n"
                    "  --warmup N            Discarded warmup transfers (10000; 0 disables)\n"
                    "  --workload NAME       all|baseline|consumer|bursty\n"
                    "  --mode NAME           both|throughput|latency\n"
                    "  --affinity NAME       none|siblings|cores (none)\n"
                    "  --output PREFIX       CSV path prefix (results/spsc)\n";
                return 0;
            }
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + std::string(option));
            const std::string_view value = argv[++i];
            if (option == "--items") config.items = checkNumber(value);
            else if (option == "--capacity") config.capacity = checkNumber(value);
            else if (option == "--repetitions") config.repetitions = checkNumber(value);
            else if (option == "--warmup") config.warmup_items = checkNumber(value);
            else if (option == "--output") output = std::string(value);
            else if (option == "--workload") {
                workloads = simple_workloads();
                if (value != "all") {
                    // remove workload if name doesnt match value, if no workloads remain throw exception
                    std::erase_if(workloads, [&](const auto& w) { return workload_name(w.type) != value; });
                    if (workloads.empty()) throw std::invalid_argument("Unknown workload");
                }
            } else if (option == "--mode") {

                if (value == "both") modes = {MeasurementMode::Throughput, MeasurementMode::Latency};
                else if (value == "throughput") modes = {MeasurementMode::Throughput};
                else if (value == "latency") modes = {MeasurementMode::Latency};
                else throw std::invalid_argument("Unknown measurement mode");
            } else if (option == "--affinity") {
                if (value == "none") config.affinity = CpuAffinityPolicy::NoAffinity;
                else if (value == "siblings") config.affinity = CpuAffinityPolicy::Hyperthreads;
                else if (value == "cores") config.affinity = CpuAffinityPolicy::TwoPhysicalCores;
                else throw std::invalid_argument("Unknown affinity policy");
            } else throw std::invalid_argument("Unknown option: " + std::string(option));
        }
        std::cout << "Running four queues; each run verifies the received sequence.\n" << std::flush;
        const auto results = run_benchmarks(config, workloads, modes);
        export_csv(results, output);
        for (const auto& r : results) {
            std::cout << r.queue << ' ' << workload_name(r.workload.type) << ' '
                      << (r.mode == MeasurementMode::Throughput ? "throughput" : "latency")
                      << " run=" << r.repetition << " items/s=" << r.items_per_second;
            if (r.latency) {
                std::cout << " p50=" << r.latency->p50_ns << "ns p99=" << r.latency->p99_ns
                          << "ns p99.9=" << r.latency->p999_ns << "ns";
            }
            std::cout << '\n';
        }
        std::cout << "Verified " << results.size() << " runs. Wrote " << output.string()
                  << ".csv\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
