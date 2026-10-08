#ifndef CONCURRENT_LAB_THREAD_UTIL_HPP
#define CONCURRENT_LAB_THREAD_UTIL_HPP

#include "harness/workload_types.hpp"
#include <thread>


namespace lab::harness {
    enum class CpuAffinityPolicy;

    struct CpuPair {
    int producer = -1;
    int consumer = -1;
};

// tell other source files that this function is defined in thread_util.cpp

// Select from CPUs allowed to this process. Pinned policies require Linux.
CpuPair select_cpus(CpuAffinityPolicy policy);
// Called by main while this worker is waiting at the starting gate.
void pin_thread(std::jthread& thread, int cpu);
const char* affinity_name(CpuAffinityPolicy policy);
} // namespace lab::harness
#endif
