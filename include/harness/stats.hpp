#ifndef CONCURRENT_LAB_STATS_HPP
#define CONCURRENT_LAB_STATS_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace lab::harness {
struct LatencyStats {
    std::size_t samples = 0;
    double p50_ns = 0;
    double p99_ns = 0;
    double p999_ns = 0;
};

// Take a copy or moved vector so caller can sort it.
// All statistics are calculated after the workers finish.
inline LatencyStats latency_stats(std::vector<double> samples) {
    if (samples.empty()) {
        throw std::invalid_argument("Latency samples must not be empty");
    }
    for (double value : samples) {
        if (!std::isfinite(value) || value < 0) {
            throw std::invalid_argument("A latency sample must be finite and nonnegative");
        }
    }
    std::sort(samples.begin(), samples.end());

    // Select the sample at rank ceil(fraction * sample count).
    auto percentile = [&](double fraction) {
        const auto rank = static_cast<std::size_t>(std::ceil(fraction * samples.size()));
        return samples[rank - 1]; // Vector indices start at zero.
    };
    return {samples.size(), percentile(0.50), percentile(0.99), percentile(0.999)};
}



// Transfer / sec. Transfer is items successfully pushed and consumed.
// Elapsed time is the total time for the run, excluding warmup and teardown.
inline double throughput_stat(std::size_t transfers, double elapsed_seconds) {
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds <= 0) {
        throw std::invalid_argument("Elapsed time must be positive and finite");
    }
    return static_cast<double>(transfers) / elapsed_seconds;
}
} // namespace lab::harness
#endif
