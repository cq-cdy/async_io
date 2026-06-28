# Test suite for talon::async_io.
# Uses doctest (single-header, C++17-compatible, fast compilation).
#
# Tests are split into four categories:
#   unit/        — no io_uring required, test individual components
#   integration/ — require io_uring and Linux kernel >= 5.11
#   stress/      — high-throughput and concurrency scenarios
#   adversarial/ — edge cases, error handling, resource exhaustion

include(FetchContent)
FetchContent_Declare(
    doctest
    URL file:///home/cdy/doctest_v2.4.11.tar.gz
    URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
)
FetchContent_MakeAvailable(doctest)

# ============================================================================
# Helper macro
# ============================================================================
function(add_async_io_test name)
    string(REPLACE "/" "_" target ${name})
    add_executable(${target} ${name}.cc)
    target_link_libraries(${target} PRIVATE talon_async_io doctest)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)

    # ASAN/UBSAN in Debug builds.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined)
    endif()

    # Timeout: integration tests may take longer under load.
    add_test(NAME ${name} COMMAND ${target})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

# ============================================================================
# Unit Tests
# ============================================================================
add_async_io_test(unit/test_kernelbuf)
add_async_io_test(unit/test_ioresult)
add_async_io_test(unit/test_function_traits)
add_async_io_test(unit/test_task_state)
add_async_io_test(unit/test_user_data)
add_async_io_test(unit/test_pools)
add_async_io_test(unit/test_done_state)

# ============================================================================
# Integration Tests
# ============================================================================
add_async_io_test(integration/test_file_io)
add_async_io_test(integration/test_tcp_basic)
add_async_io_test(integration/test_task_chain)
add_async_io_test(integration/test_timeout)
add_async_io_test(integration/test_cancel)
add_async_io_test(integration/test_retry)
add_async_io_test(integration/test_repeat_forever)

# ============================================================================
# Stress Tests
# ============================================================================
add_async_io_test(stress/test_high_throughput)
add_async_io_test(stress/test_pool_stress)
add_async_io_test(stress/test_buffer_reuse)
add_async_io_test(stress/test_aba_stress)
add_async_io_test(stress/test_pool_hammer)

# ============================================================================
# Adversarial Tests
# ============================================================================
add_async_io_test(adversarial/test_null_pointer)
add_async_io_test(adversarial/test_invalid_fd)
add_async_io_test(adversarial/test_double_submit)
add_async_io_test(adversarial/test_shutdown_stress)
add_async_io_test(adversarial/test_queue_full)
add_async_io_test(adversarial/test_lifetime)
add_async_io_test(adversarial/test_memory_leak)
add_async_io_test(adversarial/test_backpressure)
add_async_io_test(adversarial/test_signal_shutdown)
