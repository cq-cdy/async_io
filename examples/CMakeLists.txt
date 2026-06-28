add_executable(file_read read_file_example.cc)
target_link_libraries(file_read PRIVATE talon_async_io)

add_executable(file_read_large read_large_file_example.cc)
target_link_libraries(file_read_large PRIVATE talon_async_io)

add_executable(tcp_server server_tcp_example.cc)
target_link_libraries(tcp_server PRIVATE talon_async_io)

add_executable(tcp_wr tcp_wr_example.cc)
target_link_libraries(tcp_wr PRIVATE talon_async_io)
