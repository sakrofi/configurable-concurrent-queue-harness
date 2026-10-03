//
// boost_spsc_adaptor.hpp
// Adaptor to make Boost's SPSC queue satisfy the SpscQueue concept
//
// Why an adaptor?
// - Boost's API differs from your SpscQueue interface
// - You want to test different libraries with identical benchmark code
// - Adaptors let you "wrap" third-party queues to match your interface
//

#ifndef CONCURRENT_LAB_BOOST_SPSC_ADAPTOR_H
#define CONCURRENT_LAB_BOOST_SPSC_ADAPTOR_H

#include <boost/lockfree/spsc_queue.hpp>
#include <cstddef>

template <typename T>
class boost_spsc_adaptor {
public:

    using value_type = T;

    // Allocate storage before the benchmark starts timing.
    explicit boost_spsc_adaptor(std::size_t capacity) : queue_(capacity) {}
    // Boost's SPSC queue is not copyable or movable
    boost_spsc_adaptor(const boost_spsc_adaptor&) = delete;
    boost_spsc_adaptor& operator=(const boost_spsc_adaptor&) = delete;
    boost_spsc_adaptor(boost_spsc_adaptor&&) = delete;
    boost_spsc_adaptor& operator=(boost_spsc_adaptor&&) = delete;




    bool try_push(const T& item) {
        return queue_.push(item);
    }

    // Satisfy SpscQueue concept: try_pop
    // Returns: true if item was dequeued into ref, false if queue is empty
    bool try_pop(T& item) {
        return queue_.pop(item);
    }

private:
    // Capacity is chosen at construction and stays fixed during use.
    boost::lockfree::spsc_queue<T> queue_;
};

#endif // CONCURRENT_LAB_BOOST_SPSC_ADAPTOR_H
