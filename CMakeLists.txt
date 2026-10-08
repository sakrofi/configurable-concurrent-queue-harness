cmake_minimum_required(VERSION 3.20)
project(concurrent-lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


include(FetchContent)


# ==============================================================================
# 1. DEPENDENCIES
# ==============================================================================
find_package(Boost 1.70 REQUIRED)

# Pin queue dependencies to exact revisions so fresh builds use the same sources.
# Rigtorp SPSCQueue (Header-only)
FetchContent_Declare(
        rigtorp_spsc
        GIT_REPOSITORY https://github.com/rigtorp/SPSCQueue.git
        GIT_TAG        1053918dbd251fbff69b24ef27fa5d51c29ec2af
)
FetchContent_MakeAvailable(rigtorp_spsc)



FetchContent_Declare(
        readerwriterqueue
        GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue.git
        GIT_TAG        6867b56452352acf077fccd5f6cc7e3a8cfde0fb
)
FetchContent_MakeAvailable(readerwriterqueue)



# Catch2 v3 (Unit testing)
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")





# ==============================================================================
# Header-only libraries
# ==============================================================================
add_library(queue_lib INTERFACE)
target_include_directories(queue_lib INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(queue_lib INTERFACE
        Boost::boost
        SPSCQueue::SPSCQueue
        readerwriterqueue
)

# The harness now has compiled runner/export and affinity functions.
find_package(Threads REQUIRED)
add_library(harness_lib STATIC
        harness/harness_runner.cpp
        harness/thread_util.cpp
)
target_link_libraries(harness_lib PUBLIC queue_lib Threads::Threads)
target_compile_definitions(harness_lib PRIVATE LAB_BUILD_TYPE="$<CONFIG>")


# ==============================================================================
# 3. UNIT TESTS
# ==============================================================================
enable_testing()

add_executable(unit_tests
        tests/test_spsc.cpp
        tests/test_harness.cpp
)
target_link_libraries(unit_tests PRIVATE
        queue_lib
        harness_lib
        Catch2::Catch2WithMain
        Threads::Threads
)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests PROPERTIES TIMEOUT 30)

# ==============================================================================
# 4. BENCHMARK EXECUTABLE
# ==============================================================================
add_executable(benchmarks
        benchmarks/main.cpp
)
target_link_libraries(benchmarks PRIVATE harness_lib)


