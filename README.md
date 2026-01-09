# LockstepDemo - 帧同步游戏框架

## 项目简介

LockstepDemo 是一个完整的帧同步（Lockstep）游戏网络同步框架实现。帧同步是一种广泛应用于MOBA、RTS、格斗等游戏类型的网络同步技术，其核心思想是：**相同输入 + 确定性逻辑 = 相同结果**。

本项目实现了一个俯视角坦克对战Demo，支持2-4人局域网对战，完整展示了帧同步的核心技术。

## 核心原理

```
┌────────────────────────────────────────────────────────────┐
│                      帧同步核心思想                         │
├────────────────────────────────────────────────────────────┤
│  相同输入 + 确定性逻辑 = 相同结果                           │
│                                                            │
│  Client1 ──┐                    ┌── 执行相同逻辑 ── 状态A  │
│  Client2 ──┼─► Server广播帧 ──►├── 执行相同逻辑 ── 状态A  │
│  Client3 ──┘                    └── 执行相同逻辑 ── 状态A  │
└────────────────────────────────────────────────────────────┘
```

### 帧同步 vs 状态同步

| 对比项 | 帧同步 | 状态同步 |
|--------|--------|----------|
| 同步内容 | 玩家输入 | 游戏状态 |
| 带宽消耗 | 低 | 高 |
| 回放支持 | 天然支持 | 需额外记录 |
| 适用类型 | MOBA/RTS/格斗 | FPS/MMO |
| 核心难点 | 确定性计算 | 延迟补偿 |

## 项目架构

```
┌─────────────────────────────────────────┐
│              LockstepServer             │
├─────────────────────────────────────────┤
│  - UDP Socket (低延迟通信)              │
│  - Room Manager (房间管理)              │
│  - Frame Collector (输入收集)           │
│  - Frame Broadcaster (帧广播)           │
│  - Reconnect Handler (断线重连)         │
└─────────────────────────────────────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│   Client1   │ │   Client2   │ │   Client3   │
├─────────────┤ ├─────────────┤ ├─────────────┤
│ InputMgr    │ │ InputMgr    │ │ InputMgr    │
│ GameWorld   │ │ GameWorld   │ │ GameWorld   │
│ Renderer    │ │ Renderer    │ │ Renderer    │
└─────────────┘ └─────────────┘ └─────────────┘
```

## 目录结构

```
LockstepDemo/
├── common/                 # 公共模块
│   ├── Fixed.h            # Q16.16定点数库（确定性计算核心）
│   ├── Random.h           # 确定性随机数生成器
│   ├── Protocol.h         # 网络协议定义
│   ├── Network.h          # 跨平台网络封装
│   ├── Entity.h           # 游戏实体（玩家、子弹）
│   └── GameWorld.h        # 游戏世界逻辑
├── server/                 # 服务器
│   ├── LockstepServer.h   # 帧同步服务器实现
│   └── main.cpp           # 服务器入口
├── client/                 # 客户端
│   ├── LockstepClient.h   # 帧同步客户端实现
│   └── main.cpp           # 客户端入口（ASCII渲染）
├── CMakeLists.txt         # CMake构建配置
└── README.md              # 项目文档
```

## 核心技术点

### 1. 定点数运算 (Fixed.h)

浮点数在不同平台/编译器下可能产生不同结果，导致不同步。定点数使用整数模拟小数运算，确保跨平台一致性。

```cpp
// Q16.16格式：16位整数 + 16位小数
class Fixed {
    int32_t value_;  // 原始值
    static constexpr int FRAC_BITS = 16;
    static constexpr int32_t ONE = 1 << 16;  // 65536
};

// 使用示例
Fixed a = Fixed::fromFloat(3.14f);
Fixed b = Fixed::fromInt(2);
Fixed c = a * b;  // 确定性乘法
```

### 2. 确定性随机数 (Random.h)

使用线性同余生成器(LCG)，相同种子产生相同序列。

```cpp
class DeterministicRandom {
    uint32_t seed_;
    uint32_t next() {
        seed_ = seed_ * 1103515245 + 12345;
        return seed_;
    }
};
```

### 3. 帧同步流程

```
1. 客户端采集本地输入
2. 客户端发送输入到服务器
3. 服务器收集所有玩家输入
4. 服务器广播帧数据（包含所有输入）
5. 客户端收到帧数据
6. 客户端按确定性逻辑执行游戏更新
7. 所有客户端状态保持一致
```

### 4. 断线重连

服务器保存历史帧数据，客户端重连时快速追帧恢复状态。

## 编译方法

### Windows (MSVC)

```bash
cd LockstepDemo
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Windows (MinGW)

```bash
cd LockstepDemo
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make
```

### Linux

```bash
cd LockstepDemo
mkdir build && cd build
cmake ..
make
```

## 使用方法

### 启动服务器

```bash
# 默认端口9999
./lockstep_server

# 指定端口
./lockstep_server 12345
```

### 启动客户端

```bash
# 连接本地服务器
./lockstep_client

# 连接指定服务器
./lockstep_client 192.168.1.100 9999
```

### 游戏操作

| 按键 | 功能 |
|------|------|
| W | 向上移动 |
| A | 向左移动 |
| S | 向下移动 |
| D | 向右移动 |
| Space | 发射子弹 |
| Q | 退出游戏 |

## 游戏规则

- 2-4人对战，等待玩家加入后自动开始
- WASD控制移动，空格发射子弹
- 子弹击中敌方玩家造成10点伤害
- 玩家初始100血量，血量归零则死亡
- 最后存活的玩家获胜

## 网络协议

### 消息类型

| 类型 | 值 | 方向 | 说明 |
|------|-----|------|------|
| JOIN | 1 | C→S | 加入房间 |
| JOIN_ACK | 2 | S→C | 加入确认 |
| START | 3 | S→C | 游戏开始 |
| INPUT | 4 | C→S | 玩家输入 |
| FRAME | 5 | S→C | 帧数据 |
| RECONNECT | 6 | C→S | 断线重连 |
| SYNC | 7 | S→C | 同步数据 |

### 玩家输入结构 (18字节)

```
+----------+----------+---------+---------+----------+----------+
| playerId | frameId  | moveDir | actions | targetX  | targetY  |
| 4 bytes  | 4 bytes  | 1 byte  | 1 byte  | 4 bytes  | 4 bytes  |
+----------+----------+---------+---------+----------+----------+
```

## 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| LOGIC_FPS | 15 | 逻辑帧率 |
| MAX_PLAYERS | 4 | 最大玩家数 |
| MIN_PLAYERS | 2 | 开始游戏最少人数 |
| MAX_HISTORY | 1000 | 历史帧缓存数量 |

## 扩展建议

1. **图形渲染**: 集成SDL2/SFML实现图形界面
2. **预测回滚**: 实现客户端预测和服务器回滚
3. **网络优化**: 添加丢包重传、抖动缓冲
4. **录像回放**: 保存帧数据实现战斗回放
5. **反作弊**: 服务器端状态校验

## 技术亮点

- 完整的帧同步架构实现
- Q16.16定点数确保跨平台确定性
- 确定性随机数生成器
- UDP低延迟通信
- 支持断线重连追帧
- 同步校验和检测
- 跨平台支持(Windows/Linux)

## 适用场景

- MOBA类游戏（如王者荣耀、英雄联盟）
- RTS即时战略游戏
- 格斗游戏
- 回合制游戏
- 需要战斗回放的游戏

## 参考资料

- [帧同步游戏开发基础](https://gafferongames.com/post/deterministic_lockstep/)
- [定点数数学库设计](https://en.wikipedia.org/wiki/Fixed-point_arithmetic)
- [网络游戏同步技术](https://www.gabrielgambetta.com/client-server-game-architecture.html)
