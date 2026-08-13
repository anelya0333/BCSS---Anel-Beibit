cmake_minimum_required(VERSION 3.28)
project(BCSS_SIMULATOR VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define target-specific compiler warnings for our codebase
set(BCSS_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wformat=2
    -Wnull-dereference
    -Wdouble-promotion
    -Werror
)

# Dependencies: OpenSSL for SHA-256 schedule hashing
find_package(OpenSSL REQUIRED)

# GoogleTest setup via FetchContent (pinned stable release v1.15.2)
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
# For Windows/MSVC compatibility if needed, keep gtest_force_shared_crt
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Google Benchmark setup via FetchContent (pinned stable release v1.9.0)
FetchContent_Declare(
    benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.0.tar.gz
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)

# Core BCSS Research Library
add_library(bcss_core STATIC
    src/types.cpp
    src/schedule.cpp
    src/hasher.cpp
    src/dependencies.cpp
    src/validator.cpp
    src/rtc_guard.cpp
    src/search.cpp
    src/scheduler.cpp
    src/workload.cpp
    src/variants.cpp
    src/metrics.cpp
)

target_link_libraries(bcss_core PUBLIC OpenSSL::Crypto)
target_compile_options(bcss_core PRIVATE ${BCSS_WARNING_FLAGS})

# Main Executable CLI
add_executable(bcss_cli src/main.cpp)
target_link_libraries(bcss_cli PRIVATE bcss_core)
target_compile_options(bcss_cli PRIVATE ${BCSS_WARNING_FLAGS})

# CTest Setup
enable_testing()
include(GoogleTest)

# Unit Tests Executable
add_executable(bcss_unit_tests
    tests/unit/test_types.cpp
    tests/unit/test_schedule.cpp
    tests/unit/test_hasher.cpp
    tests/unit/test_dependencies.cpp
    tests/unit/test_validator.cpp
    tests/unit/test_rtc_guard.cpp
    tests/unit/test_search.cpp
    tests/unit/test_scheduler.cpp
    tests/unit/test_workload.cpp
    tests/unit/test_offline_sporadic_admission.cpp
)
target_link_libraries(bcss_unit_tests PRIVATE bcss_core gtest_main)
gtest_discover_tests(bcss_unit_tests)

# Integration Handcrafted Scenarios Executable (Scenarios 80--93)
add_executable(bcss_integration_tests
    tests/integration/test_direct_allocation.cpp
    tests/integration/test_sporadic_reclamation.cpp
    tests/integration/test_one_hop_compensation.cpp
    tests/integration/test_infeasible_compensation.cpp
    tests/integration/test_multi_hop.cpp
    tests/integration/test_exact_k.cpp
    tests/integration/test_cycle_prevention.cpp
    tests/integration/test_multi_slot.cpp
    tests/integration/test_dependencies_integration.cpp
    tests/integration/test_past_immutability.cpp
    tests/integration/test_simultaneous_requests.cpp
    tests/integration/test_rtc_one_shot_protection.cpp
    tests/integration/test_rtc_alternative_candidate.cpp
    tests/integration/test_hash_integrity.cpp
)
target_link_libraries(bcss_integration_tests PRIVATE bcss_core gtest_main)
gtest_discover_tests(bcss_integration_tests)

# Small-State Exhaustive & RTC Oracle Verification Executable
add_executable(bcss_oracle_tests
    tests/oracle/small_state_oracle.cpp
    tests/oracle/test_exhaustive_oracle.cpp
    tests/oracle/test_independent_rtc_oracle.cpp
)
target_link_libraries(bcss_oracle_tests PRIVATE bcss_core gtest_main)
gtest_discover_tests(bcss_oracle_tests)

# Property-Based Fuzzing & Invariant Executable
add_executable(bcss_property_tests
    tests/property/test_property_invariants.cpp
    tests/property/test_metamorphic.cpp
    tests/property/test_malformed_fuzzing.cpp
)
target_link_libraries(bcss_property_tests PRIVATE bcss_core gtest_main)
gtest_discover_tests(bcss_property_tests)

# Google Benchmarks Executable
add_executable(bcss_benchmarks
    benchmarks/benchmark_bcss.cpp
)
target_link_libraries(bcss_benchmarks PRIVATE bcss_core benchmark::benchmark)

# Comparison Baselines Architecture
add_subdirectory(comparisons)
