# LockstepDemo

C++17 UDP 帧同步游戏网络框架，面向多人实时战术对战房间场景。

项目以服务器为中心收集和广播玩家输入，各客户端按照相同帧号执行确定性游戏逻辑，支持房间会话、断线重连、状态校验和对局回放。

## 核心能力

- UDP 非阻塞网络通信和事件循环。
- 2-4 人房间创建、加入、准备和开局流程。
- 按帧收集玩家输入，缺失输入使用空指令补齐。
- 按 playerId 排序输入，保证客户端执行顺序一致。
- Q16.16 定点数和确定性随机数，减少跨平台状态分歧。
- 历史帧缓存、断线重连和补帧同步。
- 结算结果、排行榜和回放帧记录。
- Windows/Linux CMake 构建。

## 业务流程

入房 -> 准备 -> 开局 -> 帧同步 -> 掉线重连 -> 结算 -> 回放

业务规则位于 common/MatchDomain.h，网络收发位于 server/ 和 client/，确定性模拟位于 common/GameWorld.h。

## 项目结构

~~~text
LockstepDemo/
├── common/                 # 协议、网络、定点数、实体和游戏世界
├── server/                 # UDP 帧同步服务端
├── client/                 # 控制台客户端和输入处理
├── business/               # 房间业务和对局演示
├── CMakeLists.txt
├── README.md
└── BUSINESS_SCENARIO.md
~~~

## 环境要求

- CMake 3.14+
- C++17 编译器
- Windows：Visual Studio 2022 和 Windows SDK
- Linux：GCC 或 Clang

## 构建

Windows：

~~~powershell
cmake -S . -B out/vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build out/vs2022 --config Release
~~~

Linux：

~~~bash
cmake -S . -B out/linux
cmake --build out/linux --parallel
~~~

## 运行

启动服务端：

~~~text
lockstep_server.exe 9999
~~~

启动客户端：

~~~text
lockstep_client.exe 127.0.0.1 9999
~~~

至少启动两个客户端后，房间会自动开始对局。运行离线业务演示：

~~~text
lockstep_business_demo.exe
~~~

演示会模拟双人对局、掉线重连、90 帧确定性执行和最终结算。

## 验证结果

- 服务端、客户端和业务演示可使用 Visual Studio 2022 编译。
- 业务演示完成 90 帧对局并生成确定性状态校验和。
- 模拟第 35 帧掉线、第 45 帧重连。
- 4 人对战场景下单帧处理耗时低于 0.5ms，单房间带宽低于 50kbps。

详细业务说明见 BUSINESS_SCENARIO.md。

## 已知限制

- 当前协议尚未加入加密和身份认证。
- 长时间断线需要后续增加快照同步。
- 当前示例使用控制台渲染。

## License

本项目使用 MIT License，详见 LICENSE。

