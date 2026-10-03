#ifndef CONCURRENT_LAB_BENCHMARK_HARNESS_HPP
#define CONCURRENT_LAB_BENCHMARK_HARNESS_HPP

#include "harness/stats.hpp"
#include "harness/thread_util.hpp"
#include "harness/verifier.hpp"
#include "harness/workload_types.hpp"
#include "queues/queue_concepts.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <latch>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lab::harness {
using Clock = std::chrono::steady_clock; // elapsed without system-clock jumps

struct RunResult {
    std::string queue;
    Workload workload;
    BenchmarkConfig config;
    MeasurementMode mode;
    std::size_t repetition = 0;
    CpuPair cpus;
    double seconds = 0;
    double items_per_second = 0;
    std::uint64_t failed_pushes = 0;
    std::uint64_t failed_pops = 0;
    std::uint64_t consumer_work_result = 0;
    std::optional<LatencyStats> latency; // Empty for throughput-only runs.
};

void validate(const BenchmarkConfig& config, const Workload& workload);
std::uint64_t consumer_work(std::uint64_t state, std::size_t iterations);

// One worker writes its own result. The main thread reads it after join().
struct WorkerResult {
    std::size_t transferred = 0;
    std::uint64_t retries = 0;
    Clock::time_point finished_at;
    std::exception_ptr error; // An error exception is captured then thrown in main thread.
};

// This function is templated only on the queue with def in header so we can have multiple queue types
// Mode is an ordinary setting.
template<lab::queues::SpscQueue Queue>
RunResult run_one_measurement(const std::string& name, const BenchmarkConfig& config,
                   const Workload& workload, MeasurementMode mode,
                   CpuPair cpus, std::size_t repetition) {
    static_assert(std::same_as<typename Queue::value_type, Payload>);
    validate(config, workload);
    if (mode != MeasurementMode::Throughput && mode != MeasurementMode::Latency) {
        throw std::invalid_argument("Unknown measurement mode");
    }
    const bool is_latency_measurement = mode == MeasurementMode::Latency;
    Queue queue(config.capacity);
    std::vector<double> samples;
    if (is_latency_measurement) samples.resize(config.items); // We Allocate before timing.

    std::atomic<bool> stop{false}; // Main or a worker requests early exit on error or timeout.
    std::atomic<int> finished{0}; // Workers increment; main polls. Timing uses worker finish timestamps.
    std::latch ready(2); // Workers release once ready to confirm they have started. Main waits for both
    std::latch go(1);    // Holds workers then main releases them to start timing.
    WorkerResult produced, consumed;
    std::uint64_t consumer_state = 1; // Integer-mixing state for ConsumerWork; stays 1 for other workloads.

    // Both threads need the same start, exception handling, and finish signal.
    // 'task' is the producer or consumer lambda below, copied into its thread.
    auto launch_worker = [&](auto worker_task, WorkerResult& worker_result) {
        return std::jthread([&, worker_task] {
            //in each worker
            ready.count_down(); // tell main worker reached start gate
            go.wait(); // wait for main to finish pinning and start the timer
            try { worker_task(); }
            catch (...) {
                worker_result.error = std::current_exception();
                stop.store(true, std::memory_order_relaxed);
            }
            worker_result.finished_at = Clock::now();
            finished.fetch_add(1, std::memory_order_relaxed);
        });
    };

    std::jthread producer, consumer;
    try {
        producer = launch_worker([&] {
            std::uint64_t retries = 0;
            for (std::size_t i = 0; i < config.items; ++i) {
                if (stop.load(std::memory_order_relaxed)) return; // early exit on error or timeout
                Payload item{static_cast<std::uint64_t>(i), 0};
                if (is_latency_measurement) {
                    item.sent_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now().time_since_epoch()).count();
                }
                // Preserve the timestamp across retries: latency includes waiting for space.
                while (!queue.try_push(item)) {
                    ++retries;
                    if (stop.load(std::memory_order_relaxed)) return;
                    if (retries % 64 == 0) std::this_thread::yield();
                }
                if (workload.type == WorkloadType::Bursty &&
                    (i + 1) % workload.burst_size == 0 && i + 1 < config.items) {
                    std::this_thread::sleep_for(std::chrono::microseconds(workload.burst_gap_us));
                }
            }
            produced.transferred = config.items;
            produced.retries = retries;
        }, produced);

        consumer = launch_worker([&] {
            SequenceVerifier verifier;
            std::uint64_t retries = 0;
            for (std::size_t i = 0; i < config.items; ++i) {
                if (stop.load(std::memory_order_relaxed)) return;
                Payload item;
                while (!queue.try_pop(item)) {
                    ++retries;
                    if (stop.load(std::memory_order_relaxed)) return;
                    if (retries % 64 == 0) std::this_thread::yield();
                }
                if (is_latency_measurement) {
                    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now().time_since_epoch()).count();
                    samples[i] = static_cast<double>(now_ns - item.sent_ns);
                }
                if (!verifier.stepCheck(item.sequence)) {
                    throw std::runtime_error("Missing, duplicated or reordered item");
                }
                if (workload.type == WorkloadType::ConsumerWork) {
                    consumer_state = consumer_work(consumer_state ^ item.sequence, workload.consumer_iterations);
                }
            }
            Payload extra;
            if (!verifier.completionCheck(config.items) || queue.try_pop(extra)) {
                throw std::runtime_error("Wrong final item count");
            }
            consumed.transferred = config.items;
            consumed.retries = retries;
        }, consumed);

        // in main
        ready.wait(); // wait for both to announce they are ready to start.
        // ensures cpu affinity is applied so they will be pinned before any queue operations.
        pin_thread(producer, cpus.producer);
        pin_thread(consumer, cpus.consumer);
    } catch (...) {
        // jthread with catch joins on destruction, but we need to signal the other thread to exit early.
        stop.store(true, std::memory_order_relaxed);
        go.count_down();
        throw;
    }

    const auto begin = Clock::now();
    const auto deadline = begin + std::chrono::seconds(config.timeout_seconds);
    go.count_down(); // finally open gate so they can execute task()

    // Main has no queue work so periodically checks for timeout and finished count.
    // If a worker throws, it sets stop and exits.
    // we sleep to not burn CPU while monitoring
    while (finished.load(std::memory_order_relaxed) != 2) {
        if (Clock::now() >= deadline) stop.store(true, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Both workers have finished, either normally or early due to error or timeout.
    producer.join();
    consumer.join();
    // scope is only left after function exits so we must join
    // the threads before we can throw any exceptions they may have captured.

    if (produced.error) std::rethrow_exception(produced.error);
    if (consumed.error) std::rethrow_exception(consumed.error);
    const auto end = std::max(produced.finished_at, consumed.finished_at);
    if (produced.transferred != config.items || consumed.transferred != config.items || end > deadline) {
        throw std::runtime_error("Benchmark timed out before all items were transferred");
    }
    RunResult result{name, workload, config, mode, repetition, cpus};
    result.seconds = std::chrono::duration<double>(end - begin).count();
    result.items_per_second = throughput_stat(config.items, result.seconds);
    result.failed_pushes = produced.retries;
    result.failed_pops = consumed.retries;
    result.consumer_work_result = consumer_state; // Observable result keeps simulated work from disappearing.
    if (is_latency_measurement) result.latency = latency_stats(std::move(samples));
    return result;
}

std::vector<RunResult> run_benchmarks(const BenchmarkConfig& config,
                                     const std::vector<Workload>& workloads,
                                     const std::vector<MeasurementMode>& modes);
void export_csv(const std::vector<RunResult>& results, const std::filesystem::path& prefix);
} // namespace lab::harness
#endif
