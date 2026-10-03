#include "harness/thread_util.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

// C++ has no standard CPU-affinity API. These calls are Linux-specific.
#ifdef __linux__
#include <cerrno>
#include <pthread.h>
#include <sched.h>
#endif

namespace lab::harness {
const char* affinity_name(CpuAffinityPolicy policy) {
    switch (policy) {
        case CpuAffinityPolicy::NoAffinity: return "none";
        case CpuAffinityPolicy::Hyperthreads: return "siblings";
        case CpuAffinityPolicy::TwoPhysicalCores: return "cores";
    }
    throw std::invalid_argument("Unknown affinity policy");
}

#ifdef __linux__
namespace {
// Linux exposes the physical package and core IDs as small text files.
int topology_id(int cpu, const char* field) {
    const auto path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/" + field;
    std::ifstream file(path);
    int value;
    if (!(file >> value) || value < 0) {
        throw std::runtime_error("Cannot read CPU topology: " + path);
    }
    return value;
}
}
#endif

CpuPair select_cpus(CpuAffinityPolicy policy) {
    if (policy == CpuAffinityPolicy::NoAffinity) return {}; // -1 means no pinning.
    if (policy != CpuAffinityPolicy::Hyperthreads && policy != CpuAffinityPolicy::TwoPhysicalCores) {
        throw std::invalid_argument("Unknown affinity policy");
    }
#ifdef __linux__
    // First find which CPUs the OS allows us to use (containers may restrict this).
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        throw std::system_error(errno, std::generic_category(), "sched_getaffinity");
    }
    struct Cpu { int id, package, core; };
    std::vector<Cpu> cpus;
    for (int id = 0; id < CPU_SETSIZE; ++id) {
        if (CPU_ISSET(id, &allowed)) {
            cpus.push_back({id, topology_id(id, "physical_package_id"), topology_id(id, "core_id")});
        }
    }
    // CPU numbers alone do not tell us whether CPUs share a physical core.
    for (std::size_t i = 0; i < cpus.size(); ++i) {
        const bool want_same_core = policy == CpuAffinityPolicy::Hyperthreads;
        for (std::size_t j = i + 1; j < cpus.size(); ++j) {
            const auto& a = cpus[i];
            const auto& b = cpus[j];
            if (a.package != b.package) continue;
            const bool same_core = a.core == b.core;
            if (same_core == want_same_core) return {a.id, b.id};
        }
    }
    throw std::runtime_error("No allowed CPU pair matches affinity policy");
#else
    throw std::runtime_error("Pinned affinity is supported only on Linux");
#endif
}

void pin_thread(std::jthread& thread, int cpu) {
    if (cpu < 0) return;
#ifdef __linux__
    if (cpu >= CPU_SETSIZE) throw std::invalid_argument("CPU index exceeds CPU_SETSIZE");
    cpu_set_t set;
    CPU_ZERO(&set);     // Start with no CPUs selected.
    CPU_SET(cpu, &set); // Allow this worker to run on just this CPU.
    const int error = pthread_setaffinity_np(thread.native_handle(), sizeof(set), &set);
    if (error) throw std::system_error(error, std::generic_category(), "pthread_setaffinity_np");
#else
    throw std::runtime_error("Pinned affinity is supported only on Linux");
#endif
}
} // namespace lab::harness
