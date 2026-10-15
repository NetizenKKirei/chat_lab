# 保持可执行文件在build根目录，兼容现有运行命令和脚本。
add_executable(protocol_test protocol_test.cpp)
target_link_libraries(protocol_test PRIVATE chat_core)
set_target_properties(protocol_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME protocol_test COMMAND protocol_test)

# 压力测试需单独启动服务端，不能作为自动CTest用例。
add_executable(pressure_test pressure_test.cpp)
target_link_libraries(pressure_test PRIVATE chat_core)
set_target_properties(pressure_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
