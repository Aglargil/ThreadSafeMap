cmake_minimum_required(VERSION 3.10)
project(THREAD-SAFE-MAP)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 14)

# 开启测试
enable_testing()

include_directories(${PROJECT_SOURCE_DIR}/inc)

add_subdirectory(src)
# add_subdirectory(test)

add_test(NAME unit_test COMMAND unit_test)