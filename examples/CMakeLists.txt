# Talon Async I/O — Example Programs
#
# Each example is a standalone executable that demonstrates one or more
# library features.  Build with -DTALON_BUILD_EXAMPLES=ON.

# --- Core I/O examples ---
add_executable(file_read        read_file_example.cc)
target_link_libraries(file_read PRIVATE talon_async_io)

add_executable(file_read_large  read_large_file_example.cc)
target_link_libraries(file_read_large PRIVATE talon_async_io)

add_executable(file_write       file_write_example.cc)
target_link_libraries(file_write PRIVATE talon_async_io)

# --- TCP examples ---
add_executable(tcp_server       server_tcp_example.cc)
target_link_libraries(tcp_server PRIVATE talon_async_io)

add_executable(tcp_client       tcp_client_example.cc)
target_link_libraries(tcp_client PRIVATE talon_async_io)

add_executable(tcp_wr           tcp_wr_example.cc)
target_link_libraries(tcp_wr PRIVATE talon_async_io)

# --- Advanced patterns ---
add_executable(task_chain       task_chain_example.cc)
target_link_libraries(task_chain PRIVATE talon_async_io)

add_executable(timeout_retry    timeout_retry_example.cc)
target_link_libraries(timeout_retry PRIVATE talon_async_io)

add_executable(backpressure     backpressure_example.cc)
target_link_libraries(backpressure PRIVATE talon_async_io)

# --- Infrastructure ---
add_executable(signal_shutdown  signal_shutdown_example.cc)
target_link_libraries(signal_shutdown PRIVATE talon_async_io)

add_executable(custom_handler   custom_handler_example.cc)
target_link_libraries(custom_handler PRIVATE talon_async_io)

add_executable(buffer_pool      buffer_pool_example.cc)
target_link_libraries(buffer_pool PRIVATE talon_async_io)
