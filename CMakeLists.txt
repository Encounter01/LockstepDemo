# =============================================================================
# CMakeLists.txt - LockstepDemo 项目构建配置文件
# =============================================================================
#
# 【什么是 CMakeLists.txt？】
# CMake 是一个跨平台的构建工具，通过这个配置文件来描述：
#   - 项目的名称和版本
#   - 使用的 C++ 标准
#   - 源代码文件有哪些
#   - 需要链接哪些库
#   - 编译后的程序输出到哪里
#
# CMake 不直接编译代码，而是生成各平台的构建文件：
#   - Windows：生成 Visual Studio 的 .sln 工程文件
#   - Linux：生成 Makefile
#   - macOS：生成 Xcode 工程或 Makefile
#
# 【如何使用本文件构建项目？】
#
#   方式一：命令行构建（推荐）
#     mkdir build          # 1. 创建构建目录（不污染源码目录）
#     cd build             # 2. 进入构建目录
#     cmake ..             # 3. 生成构建文件（..指向CMakeLists.txt所在目录）
#     cmake --build .      # 4. 编译项目
#
#   方式二：Visual Studio 打开
#     直接用 VS 打开 CMakeLists.txt 所在文件夹，VS 会自动识别
#
#   方式三：CLion / VSCode
#     这些 IDE 原生支持 CMake，直接打开项目文件夹即可
#
# 【项目结构】
#
#   LockstepDemo/
#   ├── CMakeLists.txt        <-- 你正在看的这个文件
#   ├── common/               <-- 公共代码（客户端和服务器共用）
#   │   ├── Fixed.h           <-- 定点数库
#   │   ├── Random.h          <-- 确定性随机数
#   │   ├── Protocol.h        <-- 网络协议
#   │   ├── Network.h         <-- 跨平台网络封装
#   │   ├── Entity.h          <-- 游戏实体
#   │   └── GameWorld.h       <-- 游戏世界
#   ├── client/               <-- 客户端代码
#   │   ├── LockstepClient.h  <-- 客户端核心逻辑
#   │   └── main.cpp          <-- 客户端入口（含控制台渲染）
#   ├── server/               <-- 服务器代码
#   │   ├── LockstepServer.h  <-- 服务器核心逻辑
#   │   └── main.cpp          <-- 服务器入口
#   └── build/                <-- 构建输出目录（自动生成）
#
# =============================================================================

# -----------------------------------------------------------------------------
# cmake_minimum_required - 指定最低 CMake 版本要求
# -----------------------------------------------------------------------------
# VERSION 3.14 的原因：
#   - 3.14 支持我们使用的所有 CMake 特性
#   - install() 命令在 3.14 中更加完善
#   - 如果用户的 CMake 版本低于 3.14，会报错提示升级
#
# 【初学者提示】
#   这一行必须是 CMakeLists.txt 的第一个命令（注释除外）
#   如果你不确定该写什么版本，写 3.14 是一个安全的选择
cmake_minimum_required(VERSION 3.14)

# -----------------------------------------------------------------------------
# project - 定义项目名称和版本
# -----------------------------------------------------------------------------
# LockstepDemo：项目名称，会影响生成的工程文件名
# VERSION 1.0：项目版本号，可通过变量 ${PROJECT_VERSION} 访问
#
# 【CMake 自动设置的变量】
#   ${PROJECT_NAME}       = "LockstepDemo"
#   ${PROJECT_VERSION}    = "1.0"
#   ${PROJECT_SOURCE_DIR} = CMakeLists.txt 所在的目录
#   ${PROJECT_BINARY_DIR} = 构建输出目录（通常是 build/）
project(LockstepDemo VERSION 1.0)

# -----------------------------------------------------------------------------
# 设置 C++ 标准为 C++17
# -----------------------------------------------------------------------------
# CMAKE_CXX_STANDARD：指定使用的 C++ 标准版本
#   - 11：C++11（最低要求，支持 auto、lambda、智能指针等）
#   - 14：C++14（支持泛型 lambda、变量模板等）
#   - 17：C++17（支持 inline 变量、结构化绑定、if constexpr 等）★ 我们用这个
#   - 20：C++20（支持 concepts、coroutines、ranges 等）
#
# 【为什么选择 C++17？】
#   本项目使用了以下 C++17 特性：
#   1. inline 全局变量（Entity.h 中的 DIR_TABLE）
#      - C++17 之前，头文件中定义全局变量会导致"重复定义"链接错误
#      - C++17 的 inline 变量解决了这个问题
#   2. 结构化绑定（auto& [id, session] : players_）
#      - 遍历 map 时可以直接解构 key 和 value，代码更简洁
#   3. std::optional（虽然本项目未直接使用，但是好的实践）
set(CMAKE_CXX_STANDARD 17)

# CMAKE_CXX_STANDARD_REQUIRED：是否强制要求指定的 C++ 标准
#   ON  = 如果编译器不支持 C++17，直接报错（推荐）
#   OFF = 如果不支持，会回退到更低版本（可能导致编译错误难以排查）
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Windows 平台特定设置
# -----------------------------------------------------------------------------
# WIN32 是 CMake 内置变量，在 Windows 平台上自动为 TRUE
# 包括 32 位和 64 位 Windows
#
# 【为什么需要特殊设置？】
# Windows 的网络编程 API (Winsock) 与 Linux (POSIX socket) 差异很大，
# 需要通过宏定义来正确配置 Windows 的头文件行为
if(WIN32)
    # _WIN32_WINNT=0x0601：指定目标 Windows 版本为 Windows 7
    #   - 0x0601 = Windows 7
    #   - 0x0602 = Windows 8
    #   - 0x0A00 = Windows 10
    #   这个宏决定了哪些 Windows API 可用
    #   设置为 Windows 7 可以保证较好的兼容性
    #
    # 【add_definitions 的作用】
    #   等价于在每个源文件最开头加上 #define _WIN32_WINNT 0x0601
    #   -D 是编译器的"定义宏"参数
    add_definitions(-D_WIN32_WINNT=0x0601)

    # WIN32_LEAN_AND_MEAN：精简 windows.h 的内容
    #   windows.h 默认包含大量不常用的 API 声明（多媒体、DDE、RPC 等）
    #   定义这个宏后，windows.h 只包含核心 API，好处：
    #   1. 编译速度更快（减少头文件解析）
    #   2. 避免 Winsock 1.x 和 2.x 的类型冲突
    #   3. 减少命名空间污染
    add_definitions(-DWIN32_LEAN_AND_MEAN)

    # NOMINMAX：禁止 windows.h 定义 min/max 宏
    #   windows.h 中有 #define min(a,b) 和 #define max(a,b)
    #   这会与 C++ 标准库的 std::min / std::max 冲突
    #   导致编译错误或意外行为
    #   定义 NOMINMAX 后，windows.h 不再定义这两个宏
    #
    # 【经典 Bug 示例】
    #   不定义 NOMINMAX 时：
    #     int result = std::max(a, b);  // 编译错误！max 被展开为宏
    #   定义 NOMINMAX 后：
    #     int result = std::max(a, b);  // 正常工作
    add_definitions(-DNOMINMAX)
endif()

# -----------------------------------------------------------------------------
# 设置头文件搜索路径
# -----------------------------------------------------------------------------
# include_directories：告诉编译器在哪些目录下查找 #include 的头文件
#
# ${CMAKE_CURRENT_SOURCE_DIR}：当前 CMakeLists.txt 所在的目录
# /common：追加 common 子目录
#
# 设置后，代码中可以直接写：
#   #include "Fixed.h"        （而不需要写 #include "../common/Fixed.h"）
#   #include "Protocol.h"     （而不需要写 #include "../common/Protocol.h"）
#
# 【初学者提示】
#   这类似于告诉编译器："如果找不到头文件，去 common/ 目录下找"
#
# 【注意】本项目的客户端和服务器代码中实际使用了相对路径
#   （如 #include "../common/Network.h"），所以这行是冗余的备用设置
#   但保留它可以让代码有更灵活的 include 方式
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)

# =============================================================================
#                           构建目标定义
# =============================================================================
# 下面定义了两个可执行程序：服务器和客户端
# 每个 add_executable 告诉 CMake "用这些源文件编译出一个程序"

# -----------------------------------------------------------------------------
# 服务器可执行文件
# -----------------------------------------------------------------------------
# add_executable：定义一个可执行程序目标
#   第一个参数 lockstep_server：生成的程序名（Windows 下是 lockstep_server.exe）
#   后面的参数：参与编译的源文件列表
#
# 【为什么只有 main.cpp？】
#   因为 LockstepServer.h、GameWorld.h 等都是纯头文件（header-only），
#   它们通过 #include 被 main.cpp 包含，不需要单独列出。
#
#   头文件库（header-only library）的优点：
#   1. 不需要单独编译，减少构建复杂度
#   2. 模板代码必须在头文件中
#   3. 适合小型项目，简化 CMake 配置
#
#   缺点：
#   1. 修改任何头文件都会导致整个项目重新编译
#   2. 编译时间可能较长（所有代码在一个编译单元）
#   3. 不适合大型项目
#
# 【生产改进】
#   大型项目应该将 .h 和 .cpp 分离：
#   add_executable(lockstep_server
#       server/main.cpp
#       common/GameWorld.cpp
#       common/Network.cpp
#       ...
#   )
add_executable(lockstep_server
    server/main.cpp
)

# Windows 下链接 Winsock 库
# target_link_libraries：指定目标程序需要链接的库
#   ws2_32：Windows Socket 2 库（ws2_32.dll / ws2_32.lib）
#           提供 socket()、sendto()、recvfrom() 等函数
#
# 【为什么 Linux 不需要？】
#   Linux 的 socket API 是 libc 的一部分，默认就链接了
#   Windows 把网络功能放在单独的 DLL 中，需要显式链接
#
# 【初学者提示】
#   "链接库"就像是告诉编译器：
#   "我的代码里调用了 socket 函数，请帮我找到它的实现代码（在 ws2_32.lib 里）"
if(WIN32)
    target_link_libraries(lockstep_server ws2_32)
endif()

# -----------------------------------------------------------------------------
# 客户端可执行文件
# -----------------------------------------------------------------------------
# 与服务器类似，定义客户端程序
# 客户端的 main.cpp 包含了：
#   - LockstepClient.h（客户端网络逻辑）
#   - 控制台渲染代码
#   - 键盘输入处理
add_executable(lockstep_client
    client/main.cpp
)

# Windows 下客户端同样需要链接 Winsock 库
# 因为客户端也要进行网络通信（连接服务器、发送输入、接收帧数据）
if(WIN32)
    target_link_libraries(lockstep_client ws2_32)
endif()

# =============================================================================
#                           输出目录配置
# =============================================================================

# set_target_properties：设置目标的属性
#   可以同时设置多个目标（lockstep_server 和 lockstep_client）
#
# RUNTIME_OUTPUT_DIRECTORY：可执行文件的输出目录
#   ${CMAKE_BINARY_DIR}：构建目录的根路径（通常是 build/）
#   /bin：在构建目录下创建 bin 子目录
#
# 设置后，编译生成的程序会在：
#   build/bin/lockstep_server.exe
#   build/bin/lockstep_client.exe
#
# 【为什么要设置输出目录？】
#   1. 不设置时，不同构建配置（Debug/Release）的程序可能散落在不同目录
#   2. 统一到 bin/ 目录下便于查找和运行
#   3. 服务器和客户端在同一目录，方便本地测试
#
# 【初学者提示】
#   编译完成后，去 build/bin/ 目录下就能找到可执行文件
#   先运行 lockstep_server.exe，再运行 lockstep_client.exe
set_target_properties(lockstep_server lockstep_client
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# =============================================================================
#                           安装配置
# =============================================================================

# install：定义"安装"规则
#   "安装"是指将编译好的程序复制到系统目录，使其可以全局使用
#
# TARGETS：要安装的目标列表
# RUNTIME DESTINATION bin：可执行文件安装到 <prefix>/bin 目录
#
# 安装命令：
#   cmake --install build              # 安装到默认位置（Linux: /usr/local/bin）
#   cmake --install build --prefix /opt/lockstep  # 安装到指定位置
#
# 【初学者提示】
#   开发阶段通常不需要 install，直接在 build/bin/ 目录运行即可
#   install 主要用于部署到服务器或打包发布
install(TARGETS lockstep_server lockstep_client
    RUNTIME DESTINATION bin
)

# 如果项目有资源文件（图片、配置文件等），可以用下面的命令一起安装
# 当前项目是纯控制台程序，没有资源文件，所以注释掉了
# install(DIRECTORY resources/ DESTINATION share/lockstep)

# =============================================================================
#                           构建信息输出
# =============================================================================

# message：在 CMake 配置阶段（运行 cmake .. 时）输出信息
#   STATUS：信息级别，会显示为 "-- 内容"
#   其他级别：WARNING（警告）、FATAL_ERROR（致命错误，终止配置）
#
# 【用途】
#   运行 cmake .. 后会看到类似输出：
#   -- ========================================
#   -- LockstepDemo Configuration:
#   --   - C++ Standard: 17
#   --   - Build Type: Debug
#   -- ========================================
#
# 这帮助开发者确认配置是否正确
#
# ${CMAKE_CXX_STANDARD}：前面设置的 C++ 标准版本（17）
# ${CMAKE_BUILD_TYPE}：构建类型，可通过 -DCMAKE_BUILD_TYPE=Release 指定
#   - Debug：包含调试信息，无优化（开发阶段用）
#   - Release：无调试信息，开启优化（发布时用）
#   - RelWithDebInfo：有调试信息且有优化（性能分析用）
message(STATUS "========================================")
message(STATUS "LockstepDemo Configuration:")
message(STATUS "  - C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  - Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")

# =============================================================================
#                           初学者快速上手指南
# =============================================================================
#
# 【第一步：安装工具】
#   Windows:
#     1. 安装 Visual Studio 2019/2022（社区版免费）
#        - 安装时勾选"C++桌面开发"工作负载
#     2. 安装 CMake（https://cmake.org/download/）
#        - 安装时勾选"Add CMake to PATH"
#
#   Linux (Ubuntu):
#     sudo apt update
#     sudo apt install build-essential cmake
#
# 【第二步：编译项目】
#   打开命令行，进入项目目录：
#
#     cd C:\Users\13007\Desktop\c++项目\LockstepDemo
#     mkdir build
#     cd build
#     cmake ..
#     cmake --build .
#
# 【第三步：运行游戏】
#   1. 先启动服务器：
#      build\bin\Debug\lockstep_server.exe
#
#   2. 再启动客户端（新开一个命令行窗口）：
#      build\bin\Debug\lockstep_client.exe
#
#   3. 再启动第二个客户端（再开一个窗口）：
#      build\bin\Debug\lockstep_client.exe
#
#   当两个客户端都连上后，游戏自动开始！
#
# 【常见问题】
#   Q: cmake .. 报错 "No CMAKE_CXX_COMPILER could be found"
#   A: 没有安装 C++ 编译器，请安装 Visual Studio 或 g++
#
#   Q: 编译报错 "ws2_32.lib not found"
#   A: Windows SDK 未正确安装，重新安装 Visual Studio
#
#   Q: 运行时连接不上服务器
#   A: 检查防火墙是否阻止了 9999 端口，或者服务器是否在运行
#
# =============================================================================

# ҵ�񳡾���ʾ������״̬����ָ��У�顢���߱�����������طż�¼��
add_executable(lockstep_business_demo
    business/main.cpp
)
target_include_directories(lockstep_business_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)
