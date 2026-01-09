# LockstepDemo 帧同步游戏框架 - 项目讲解

## 一、项目概述

LockstepDemo 是一个完整的**帧同步（Lockstep）游戏网络框架**实现，使用 C++17 开发，支持 2-4 人局域网对战。该项目展示了 MOBA、RTS 等游戏类型所需的帧同步核心技术。

### 核心理念
> **相同输入 + 确定性逻辑 = 相同结果**

所有客户端执行相同的输入序列，通过确定性计算保证游戏状态完全一致。

---

## 二、项目结构

```
LockstepDemo/
├── common/                 # 公共模块（核心算法库）
│   ├── Fixed.h            # Q16.16 定点数库
│   ├── Random.h           # 确定性随机数生成器
│   ├── Protocol.h         # 网络协议定义
│   ├── Network.h          # 跨平台网络封装
│   ├── Entity.h           # 游戏实体定义
│   └── GameWorld.h        # 游戏世界逻辑
├── server/                 # 服务器（帧管理和广播）
│   ├── LockstepServer.h   # 帧同步服务器
│   └── main.cpp           # 服务器入口
├── client/                 # 客户端（输入与渲染）
│   ├── LockstepClient.h   # 帧同步客户端
│   └── main.cpp           # 客户端入口
├── CMakeLists.txt         # CMake 构建配置
└── README.md              # 项目文档
```

---

## 三、核心技术实现

### 3.1 定点数运算（Fixed.h）

**为什么需要定点数？**

浮点数在不同平台/编译器下可能产生微小的计算差异，这在帧同步中会导致"蝴蝶效应"——微小的差异随时间累积，最终导致严重的状态不同步。

**实现方案：Q16.16 格式**

```cpp
class Fixed {
    int32_t value_;                    // 内部存储
    static constexpr int FRAC_BITS = 16;
    static constexpr int32_t ONE = 1 << 16;  // 65536

    // 乘法：使用 64 位中间结果防止溢出
    Fixed operator*(Fixed other) const {
        int64_t result = (int64_t)value_ * other.value_;
        return raw(result >> FRAC_BITS);
    }
};
```

- **精度**：1/65536 ≈ 0.0000153
- **范围**：-32768 到 32767
- **特点**：所有运算完全确定性，跨平台一致

### 3.2 确定性随机数（Random.h）

**算法**：线性同余生成器（LCG）

```cpp
uint32_t next() {
    seed_ = seed_ * 1103515245 + 12345;  // glibc 标准参数
    return seed_;
}
```

- 相同种子产生完全相同的序列
- 服务器在游戏开始时广播随机种子
- 所有客户端使用相同种子，保证随机结果一致

### 3.3 网络协议（Protocol.h）

**消息类型：**

| 类型 | 方向 | 说明 |
|------|------|------|
| JOIN | C→S | 加入房间请求 |
| JOIN_ACK | S→C | 分配 playerId |
| START | S→C | 游戏开始 + 随机种子 |
| INPUT | C→S | 玩家输入帧 |
| FRAME | S→C | 帧数据广播 |
| RECONNECT | C→S | 断线重连请求 |
| SYNC | S→C | 重连同步帧 |
| HEARTBEAT | C→S | 心跳包 |

**玩家输入结构（18 字节）：**

```
┌──────────┬──────────┬─────────┬─────────┬──────────┬──────────┐
│ playerId │ frameId  │ moveDir │ actions │ targetX  │ targetY  │
│ 4 bytes  │ 4 bytes  │ 1 byte  │ 1 byte  │ 4 bytes  │ 4 bytes  │
└──────────┴──────────┴─────────┴─────────┴──────────┴──────────┘
```

### 3.4 帧同步流程

**服务器端 tick() 方法：**

```cpp
void tick() {
    FrameData frame;
    frame.frameId = currentFrame_;

    // 1. 收集所有玩家输入（未收到则使用空输入）
    for (auto& [id, session] : players_) {
        if (session.inputReceived) {
            frame.inputs.push_back(session.lastInput);
        } else {
            PlayerInput empty;
            empty.moveDir = 8;  // 停止
            frame.inputs.push_back(empty);
        }
    }

    // 2. 按 playerId 排序（确保确定性）
    std::sort(frame.inputs.begin(), frame.inputs.end(), ...);

    // 3. 广播给所有客户端
    broadcast(frame);

    // 4. 保存到历史缓冲区
    frameHistory_.push_back(frame);
    currentFrame_++;
}
```

**关键参数：**
- 逻辑帧率：15 FPS（66ms/帧）
- 最大玩家：4 人
- 历史缓存：1000 帧（约 67 秒）

---

## 四、游戏逻辑

### 4.1 实体系统

```
Entity（基类）
├── Player（玩家）
│   ├── hp / maxHp
│   ├── attackCooldown
│   └── kills / deaths
└── Bullet（子弹）
    ├── damage
    └── lifeTime
```

### 4.2 碰撞检测

```cpp
bool collidesWith(const Entity& other) const {
    FixedVec2 diff = pos - other.pos;
    Fixed distSq = diff.lengthSq();          // 距离平方
    Fixed radiusSum = radius + other.radius;
    return distSq < radiusSum * radiusSum;   // 避免 sqrt 计算
}
```

### 4.3 游戏规则

- 地图：800×600 像素
- 初始血量：100
- 移动速度：5.0
- 攻击伤害：10
- 攻击冷却：10 帧
- 胜负判定：存活玩家数 ≤ 1

---

## 五、状态同步校验

```cpp
uint32_t hash() const {
    uint32_t h = id;
    h = h * 31 + (uint32_t)pos.x.rawValue();
    h = h * 31 + (uint32_t)pos.y.rawValue();
    h = h * 31 + (uint32_t)vel.x.rawValue();
    h = h * 31 + (uint32_t)vel.y.rawValue();
    h = h * 31 + (uint32_t)hp;
    h = h * 31 + (alive ? 1 : 0);
    return h;
}
```

服务器计算并广播 checksum，客户端本地校验，不一致则表示发生了 **Desync（不同步）**。

---

## 六、断线重连机制

1. 客户端发送 `RECONNECT(playerId, lastFrame)`
2. 服务器从历史缓冲区查找缺失的帧
3. 发送 `SYNC` 消息包含所有缺失帧
4. 客户端快速追帧，恢复到当前状态

---

## 七、跨平台支持

```cpp
#ifdef _WIN32
    // Windows: Winsock2 API
    using socket_t = SOCKET;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    // Linux: BSD Socket API
    using socket_t = int;
    fcntl(sock, F_SETFL, O_NONBLOCK);
#endif
```

---

## 八、性能特性

### 网络带宽

```
4 人游戏，15 FPS：
├─ 上行：285 bytes/秒/客户端 ≈ 2.3 kbps
├─ 下行：1230 bytes/秒/客户端 ≈ 9.8 kbps
└─ 总计：<15 kbps/客户端（极其轻量）
```

### 内存占用

```
单客户端：<200KB
├─ players vector：~800B
├─ bullets vector：<5KB（通常 <100 个）
└─ frameHistory_（服务器）：~100KB
```

---

## 九、编译运行

### Windows
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
.\bin\lockstep_server.exe
.\bin\lockstep_client.exe 127.0.0.1 9999
```

### Linux
```bash
mkdir build && cd build
cmake ..
make
./bin/lockstep_server
./bin/lockstep_client 127.0.0.1 9999
```

### 操作说明
| 键 | 功能 |
|----|------|
| W/A/S/D | 移动 |
| Space | 攻击 |
| Q | 退出 |

---

## 十、技术亮点总结

1. **确定性计算**：定点数 + 确定性随机数，保证跨平台一致
2. **输入排序**：按 playerId 排序，消除处理顺序不确定性
3. **UDP 低延迟**：非阻塞 UDP 通信，适合实时游戏
4. **断线重连**：历史帧缓存支持快速追帧恢复
5. **状态校验**：Checksum 机制检测不同步
6. **跨平台**：Windows/Linux 无缝兼容
