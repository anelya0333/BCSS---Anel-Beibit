# Comparisons CMakeLists.txt

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/static_direct/include
    ${CMAKE_CURRENT_SOURCE_DIR}/slot_shifting/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dtss/include
    ${CMAKE_CURRENT_SOURCE_DIR}/affine_envelope/include
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/include
    ${CMAKE_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)

# Core Comparison Schedulers Library
add_library(bcss_comparisons STATIC
    common/src/neutral_model.cpp
    common/src/neutral_validator.cpp
    adapters/src/workload_adapter.cpp
    static_direct/src/static_direct_scheduler.cpp
    slot_shifting/src/slot_shifting_scheduler.cpp
    dtss/src/dtss_scheduler.cpp
    affine_envelope/src/affine_envelope_scheduler.cpp
)

target_link_libraries(bcss_comparisons PUBLIC bcss_core OpenSSL::Crypto)
target_compile_options(bcss_comparisons PRIVATE ${BCSS_WARNING_FLAGS})

# Comparison CLI Dispatcher Executable
add_executable(comparison_runner tools/comparison_cli.cpp)
target_link_libraries(comparison_runner PRIVATE bcss_comparisons)
target_compile_options(comparison_runner PRIVATE ${BCSS_WARNING_FLAGS})

# One-process-per-run evaluator used by the resumable Python campaign driver.
add_executable(evaluation_worker evaluation/evaluation_worker.cpp)
target_link_libraries(evaluation_worker PRIVATE bcss_comparisons)
target_compile_options(evaluation_worker PRIVATE ${BCSS_WARNING_FLAGS})

add_executable(preflight_mechanisms evaluation/preflight_mechanisms.cpp)
target_link_libraries(preflight_mechanisms PRIVATE bcss_comparisons)
target_compile_options(preflight_mechanisms PRIVATE ${BCSS_WARNING_FLAGS})

# Unit & Cross-Algorithm Tests
add_executable(comparison_unit_tests
    static_direct/tests/test_static_direct.cpp
    slot_shifting/tests/test_slot_shifting.cpp
    dtss/tests/test_dtss.cpp
    affine_envelope/tests/test_affine_envelope.cpp
    tests/test_cross_algorithm.cpp
    tests/test_fairness_fingerprint.cpp
    tests/test_randomized_validity.cpp
)
target_link_libraries(comparison_unit_tests PRIVATE bcss_comparisons gtest_main)
gtest_discover_tests(comparison_unit_tests)
