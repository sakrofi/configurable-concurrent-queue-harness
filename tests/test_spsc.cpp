#include "queues/implementations/boost_spsc_adaptor.hpp"
#include "queues/implementations/moody_spsc_adaptor.hpp"
#include "queues/implementations/rigtorp_spsc_adaptor.hpp"
#include "queues/queue_concepts.hpp"
#include "queues/implementations/spsc_v1.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <tuple>
#include <barrier>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>

// Add each adapter here once it is implemented. All use int test payloads.
using QueueTypes = std::tuple<boost_spsc_adaptor<int>,
                              moody_spsc_adaptor<int>,
                              rigtorp_spsc_adaptor<int>,
                              spsc_v1<int>>;

// These tests additionally require the requested capacity to be exact.
// ReaderWriterQueue reserves at least the requested capacity, so belongs only
// in QueueTypes unless its adapter later guarantees an exact limit.
using ExactCapacityQueueTypes = std::tuple<boost_spsc_adaptor<int>,
                                           rigtorp_spsc_adaptor<int>,
                                           spsc_v1<int>>;

// Share the repeated assertions, keeping each test's capacity choice visible.
template<lab::queues::SpscQueue Queue>
void check_fill_and_drain(Queue& queue, int usable_capacity, int cycles) {
    for (int cycle = 0; cycle < cycles; ++cycle) {
        const int base = cycle * usable_capacity;
        for (int i = 0; i < usable_capacity; ++i) {
            REQUIRE(queue.try_push(base + i));
        }
        REQUIRE_FALSE(queue.try_push(-1));
        int output = -1;
        for (int i = 0; i < usable_capacity; ++i) {
            REQUIRE(queue.try_pop(output));
            REQUIRE(output == base + i);
        }
        REQUIRE_FALSE(queue.try_pop(output));
    }
}

TEST_CASE("Custom SPSC: invalid capacities are rejected", "[spsc][custom]") {
    REQUIRE_THROWS_AS(spsc_v1<int>(0), std::invalid_argument);
    REQUIRE_THROWS_AS(spsc_v1<int>(std::numeric_limits<std::size_t>::max()),
                      std::length_error);
}

TEMPLATE_LIST_TEST_CASE("SPSC: two threads transfer an ordered sequence",
                        "[spsc][concurrent]", QueueTypes) {
    const int capacity = GENERATE(1, 3, 64);
    TestType queue(capacity);
    constexpr int count = 100000;
    std::barrier start(2); // Producer thread + this test thread (the consumer).
    bool producer_complete = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto timed_out = [&] { return std::chrono::steady_clock::now() >= deadline; };

    std::jthread producer([&](std::stop_token stop) {
        start.arrive_and_wait();
        for (int i = 0; i < count; ++i) {
            if (stop.stop_requested() || timed_out()) return;
            while (!queue.try_push(i)) {
                if (stop.stop_requested() || timed_out()) return;
                std::this_thread::yield();
            }
        }
        producer_complete = true;
    });

    // Keep assertions on the test thread. If one fails, jthread's destructor
    // requests stop and joins; the producer checks that request in its loops.
    start.arrive_and_wait();
    for (int expected = 0; expected < count; ++expected) {
        int output = -1;
        while (!queue.try_pop(output)) {
            REQUIRE_FALSE(timed_out());
            std::this_thread::yield();
        }
        REQUIRE(output == expected);
    }
    producer.join(); // Makes producer_complete safe to read.
    REQUIRE(producer_complete);
    int extra = -1;
    REQUIRE_FALSE(queue.try_pop(extra));
}

TEMPLATE_LIST_TEST_CASE("SPSC: interface and empty queue", "[spsc]", QueueTypes) {
    static_assert(lab::queues::SpscQueue<TestType>);
    TestType queue(4);
    int value = -1;
    REQUIRE_FALSE(queue.try_pop(value));
}

TEMPLATE_LIST_TEST_CASE("SPSC: push and pop preserve a value", "[spsc]", QueueTypes) {
    TestType queue(4);
    const int input = 42;
    REQUIRE(queue.try_push(input));

    int output = -1;
    REQUIRE(queue.try_pop(output));
    REQUIRE(output == input);
    REQUIRE_FALSE(queue.try_pop(output));
}

TEMPLATE_LIST_TEST_CASE("SPSC: fill until full, drain in order, and reuse",
                        "[spsc]", QueueTypes) {
    TestType queue(3);
    // A test safety limit, not an assumed queue capacity. It catches an
    // adapter that always reports success or accidentally enables growth.
    constexpr int push_limit = 4096;
    for (int cycle = 0; cycle < 5; ++cycle) {
        const int base = cycle * push_limit;
        int accepted = 0;
        while (accepted < push_limit && queue.try_push(base + accepted)) {
            ++accepted;
        }
        REQUIRE(accepted >= 3);
        REQUIRE(accepted < push_limit);
        REQUIRE_FALSE(queue.try_push(-999));

        int output = -1;
        for (int i = 0; i < accepted; ++i) {
            REQUIRE(queue.try_pop(output));
            REQUIRE(output == base + i);
        }
        REQUIRE_FALSE(queue.try_pop(output));
    }
}

TEMPLATE_LIST_TEST_CASE("SPSC: full queues reject pushes and drain in FIFO order",
                        "[spsc][exact-capacity]", ExactCapacityQueueTypes) {
    const int capacity = GENERATE(1, 2, 3, 4, 8, 1024);
    TestType queue(capacity);

    check_fill_and_drain(queue, capacity, 5);
}

TEMPLATE_LIST_TEST_CASE("SPSC: wraparound preserves items still in the queue",
                        "[spsc][exact-capacity]", ExactCapacityQueueTypes) {
    TestType queue(3);
    REQUIRE(queue.try_push(10));
    REQUIRE(queue.try_push(11));
    REQUIRE(queue.try_push(12));

    int output = -1;
    REQUIRE(queue.try_pop(output));
    REQUIRE(output == 10);
    REQUIRE(queue.try_pop(output));
    REQUIRE(output == 11);

    REQUIRE(queue.try_push(13));
    REQUIRE(queue.try_push(14));
    REQUIRE_FALSE(queue.try_push(15));

    for (int expected = 12; expected <= 14; ++expected) {
        REQUIRE(queue.try_pop(output));
        REQUIRE(output == expected);
    }
    REQUIRE_FALSE(queue.try_pop(output));
}
