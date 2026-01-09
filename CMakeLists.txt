cmake_minimum_required(VERSION 3.14)
project(LockstepDemo VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows特定设置
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# 公共头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)

# ============ 服务器 ============
add_executable(lockstep_server
    server/main.cpp
)

if(WIN32)
    target_link_libraries(lockstep_server ws2_32)
endif()

# ============ 客户端 ============
add_executable(lockstep_client
    client/main.cpp
)

if(WIN32)
    target_link_libraries(lockstep_client ws2_32)
endif()

# 设置输出目录
set_target_properties(lockstep_server lockstep_client
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 安装目标
install(TARGETS lockstep_server lockstep_client
    RUNTIME DESTINATION bin
)

# 复制资源文件（如果有）
# install(DIRECTORY resources/ DESTINATION share/lockstep)

message(STATUS "========================================")
message(STATUS "LockstepDemo Configuration:")
message(STATUS "  - C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  - Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")
