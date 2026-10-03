#ifndef CONCURRENT_LAB_QUEUE_CONCEPTS_HPP
#define CONCURRENT_LAB_QUEUE_CONCEPTS_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace lab::queues {
    template <typename Q>
    concept SpscQueue = requires(
        std::size_t capacity,
        Q& queue,
        const typename Q::value_type& in,
        typename Q::value_type& out
    ) {
        typename Q::value_type;
        { Q{capacity} };  // constructable with a capacity
        { queue.try_push(in) } -> std::convertible_to<bool>;
        { queue.try_pop(out) } -> std::convertible_to<bool>;
    };
}

#endif // CONCURRENT_LAB_QUEUE_CONCEPTS_HPP