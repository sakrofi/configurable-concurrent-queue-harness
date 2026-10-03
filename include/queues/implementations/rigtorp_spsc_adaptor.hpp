#ifndef CONCURRENT_LAB_RIGTORP_SPSC_ADAPTOR_HPP
#define CONCURRENT_LAB_RIGTORP_SPSC_ADAPTOR_HPP

#include <rigtorp/SPSCQueue.h>
#include <cstddef>
#include <utility>

template <typename T>
class rigtorp_spsc_adaptor {
public:
    using value_type = T;

    explicit rigtorp_spsc_adaptor(std::size_t capacity) : queue_(capacity) {}

    rigtorp_spsc_adaptor(const rigtorp_spsc_adaptor&) = delete;
    rigtorp_spsc_adaptor& operator=(const rigtorp_spsc_adaptor&) = delete;
    rigtorp_spsc_adaptor(rigtorp_spsc_adaptor&&) = delete;
    rigtorp_spsc_adaptor& operator=(rigtorp_spsc_adaptor&&) = delete;




    bool try_push(const T& item) {
        return queue_.try_push(item);
    }

    bool try_pop(T& item) {
        T* front = queue_.front();
        if (front == nullptr) {
            return false;
        }
        item = std::move(*front);
        queue_.pop();
        return true;
    }

private:
    rigtorp::SPSCQueue<T> queue_;
};

#endif // CONCURRENT_LAB_RIGTORP_SPSC_ADAPTOR_HPP
