//
// Created by samuel on 31/07/2026.
//

#ifndef CONCURRENT_LAB_SPSC_V1_H
#define CONCURRENT_LAB_SPSC_V1_H


#include <vector>
#include <cstddef>
#include <utility>
#include <limits>
#include <stdexcept>





#include <array>
#include <atomic>
#include <cstddef>
#include <new> // hardware_destructuive_infrerence
#include <type_traits>

//**
// a amodern c++ spsc quue type safe
// should be size configurable


// this is a naive approach to an atomic ring buffer
// like other queues accepts contract and uses differ

#include <cstdint>

#include <atomic>


template<typename T>
class spsc_v1 {




public:


    // Exactly slots usable entries, plus one spare to distinguish full from empty.
    explicit spsc_v1(const size_t slots)
        : capacity_(storage_size(slots)),
          buffer_(capacity_){}

    using value_type = T;


    bool try_push(const T& item) {

        const size_t tail = tail_.load(std::memory_order_relaxed);

        const size_t next_tail = next_index(tail);

        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        // Use the index already loaded, avoiding a second atomic load.
        buffer_[tail] = item;

        // Publish the item only after its write is complete.
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }


    // checks current head then reads
    bool try_pop(T& item) {

        // Only the consumer writes head_, so its own index needs no acquire.
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next_head = next_index(head);

        // Acquire the producer's published index before reading its item.
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        item = std::move(buffer_[head]);
        // Finish reading before the producer can reuse this slot.
        head_.store(next_head, std::memory_order_release);
        return true;
    }




private:

    // The ring no longer needs power-of-two storage or a bit mask.
    std::size_t next_index(std::size_t index) const {
        ++index;
        return index == capacity_ ? 0 : index;
    }

    // Check that adding the spare slot is representable before allocating.
    static std::size_t storage_size(std::size_t requested) {
        if (requested == 0) {
            throw std::invalid_argument("SPSC capacity must be positive");
        }
        if (requested == std::numeric_limits<std::size_t>::max()) {
            throw std::length_error("SPSC capacity is too large");
        }
        return requested + 1;
    }

    const std::size_t capacity_; // Storage slots, including the spare.
    // Align each index to the implementation's constructive interference size.
    // This value is not necessarily 64 bytes and does not guarantee separate cache lines.
    alignas(std::hardware_constructive_interference_size) std::atomic<size_t> head_{0}; // next slot to read
    alignas(std::hardware_constructive_interference_size) std::atomic<size_t> tail_{0};  // next slot to write
    std::vector<T> buffer_;
};









#endif // CONCURRENT_LAB_SPSC_V1_H
