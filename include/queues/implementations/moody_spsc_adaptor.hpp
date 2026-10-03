#ifndef CONCURRENT_LAB_MOODY_SPSC_ADAPTOR_HPP
#define CONCURRENT_LAB_MOODY_SPSC_ADAPTOR_HPP

#include <readerwriterqueue.h>
#include <cstddef>

template <typename T>
class moody_spsc_adaptor {
public:
    using value_type = T;

    // Reserve storage before timing; actual capacity may be larger.
    explicit moody_spsc_adaptor(std::size_t capacity) : queue_(capacity) {}

    moody_spsc_adaptor(const moody_spsc_adaptor&) = delete;
    moody_spsc_adaptor& operator=(const moody_spsc_adaptor&) = delete;
    moody_spsc_adaptor(moody_spsc_adaptor&&) = delete;
    moody_spsc_adaptor& operator=(moody_spsc_adaptor&&) = delete;

    bool try_push(const T& item) {
        // Unlike enqueue(), try_enqueue() never allocates more storage.
        return queue_.try_enqueue(item);
    }

    bool try_pop(T& item) {
        return queue_.try_dequeue(item);
    }

private:
    moodycamel::ReaderWriterQueue<T> queue_;
};

#endif // CONCURRENT_LAB_MOODY_SPSC_ADAPTOR_HPP


