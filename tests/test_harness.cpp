#include "harness/benchmark_harness.hpp"
#include "queues/implementations/boost_spsc_adaptor.hpp"
#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <limits>
#ifdef __linux__
#include <sched.h>
#endif

using namespace lab::harness;

TEST_CASE("Stats: nearest-rank latency percentiles", "[harness]") {
    std::vector<double> samples(1000);
    std::iota(samples.begin(), samples.end(), 1.0);
    std::reverse(samples.begin(), samples.end());
    const auto stats = latency_stats(samples);
    REQUIRE(stats.samples == 1000);
    REQUIRE(stats.p50_ns == 500);
    REQUIRE(stats.p99_ns == 990);
    REQUIRE(stats.p999_ns == 999);
    REQUIRE(latency_stats({7}).p999_ns == 7);
    REQUIRE_THROWS_AS(latency_stats({}), std::invalid_argument);
    REQUIRE_THROWS_AS(latency_stats({-1}), std::invalid_argument);
    REQUIRE(throughput_stat(100, 0.5) == 200);
    REQUIRE_THROWS_AS(throughput_stat(100, 0), std::invalid_argument);
}

TEST_CASE("Verifier: missing and duplicate values fail", "[harness]") {
    SequenceVerifier verifier;
    REQUIRE(verifier.stepCheck(0));
    REQUIRE_FALSE(verifier.completionCheck(2));
    REQUIRE(verifier.stepCheck(1));
    REQUIRE(verifier.completionCheck(2));
    REQUIRE_FALSE(verifier.stepCheck(1));
    REQUIRE_FALSE(verifier.completionCheck(2));
    SequenceVerifier reordered;
    REQUIRE_FALSE(reordered.stepCheck(1));
}

TEST_CASE("Harness: throughput and latency return verified results", "[harness]") {
    BenchmarkConfig config;
    config.items = 1000;
    config.capacity = 3;
    config.warmup_items = 0;
    config.repetitions = 1;
    const auto fast = run_one_measurement<boost_spsc_adaptor<Payload>>(
        "boost", config, Workload{}, MeasurementMode::Throughput, {}, 1);
    REQUIRE(fast.seconds > 0);
    REQUIRE(fast.items_per_second > 0);
    REQUIRE_FALSE(fast.latency.has_value());
    const auto timed = run_one_measurement<boost_spsc_adaptor<Payload>>(
        "boost", config, Workload{}, MeasurementMode::Latency, {}, 1);
    REQUIRE(timed.latency.has_value());
    REQUIRE(timed.latency->samples == config.items);
    REQUIRE(timed.latency->p50_ns >= 0);
    REQUIRE(timed.latency->p99_ns >= timed.latency->p50_ns);
    REQUIRE(timed.latency->p999_ns >= timed.latency->p99_ns);
}

// Deliberately broken queues ensure verification and timeout paths really work.
class CorruptQueue {
public:
    using value_type = Payload;
    explicit CorruptQueue(std::size_t capacity) : queue_(capacity) {}
    bool try_push(const Payload& item) { return queue_.try_push(item); }
    bool try_pop(Payload& item) {
        if (!queue_.try_pop(item)) return false;
        ++item.sequence;
        return true;
    }
private:
    boost_spsc_adaptor<Payload> queue_;
};
class DroppingQueue {
public:
    using value_type = Payload;
    explicit DroppingQueue(std::size_t) {}
    bool try_push(const Payload&) { return true; }
    bool try_pop(Payload&) { return false; }
};

TEST_CASE("Harness: corrupt or lost items cannot produce a result", "[harness]") {
    BenchmarkConfig config;
    config.items = 100;
    config.capacity = 3;
    config.timeout_seconds = 1;
    REQUIRE_THROWS_AS((run_one_measurement<CorruptQueue>(
        "broken", config, Workload{}, MeasurementMode::Throughput, {}, 1)), std::runtime_error);
    REQUIRE_THROWS_AS((run_one_measurement<DroppingQueue>(
        "broken", config, Workload{}, MeasurementMode::Throughput, {}, 1)), std::runtime_error);
}

TEST_CASE("Harness: reject invalid settings", "[harness]") {
    BenchmarkConfig config;
    Workload workload;
    workload.type = static_cast<WorkloadType>(-1);
    REQUIRE_THROWS_AS(validate(config, workload), std::invalid_argument);
    workload.type = WorkloadType::Bursty;
    workload.burst_size = 0;
    REQUIRE_THROWS_AS(validate(config, workload), std::invalid_argument);
    workload = Workload{};
    config.items = 0;
    REQUIRE_THROWS_AS(validate(config, workload), std::invalid_argument);
}

TEST_CASE("Harness: failed affinity setup releases waiting workers", "[harness]") {
    BenchmarkConfig config;
    config.items = 10;
    const CpuPair invalid_cpus{std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
    REQUIRE_THROWS((run_one_measurement<boost_spsc_adaptor<Payload>>(
        "boost", config, Workload{}, MeasurementMode::Throughput, invalid_cpus, 1)));
}

#ifdef __linux__
TEST_CASE("Affinity: main pins a waiting worker to the requested CPU", "[harness]") {
    const int cpu = sched_getcpu();
    REQUIRE(cpu >= 0);
    int observed_cpu = -1;
    std::latch go(1);
    std::jthread worker([&] {
        go.wait();
        observed_cpu = sched_getcpu();
    });
    try {
        pin_thread(worker, cpu);
    } catch (...) {
        go.count_down(); // Let the thread exit before its destructor joins it.
        throw;
    }
    go.count_down();
    worker.join();
    REQUIRE(observed_cpu == cpu);
}
#endif
