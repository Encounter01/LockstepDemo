# LockstepDemo 实际业务场景

## 场景定位

项目模拟一个多人实时战术对战房间。玩家先进入房间并准备，服务端在满足人数和准备条件后开局；对局中只同步玩家指令，所有节点按相同帧号执行确定性逻辑；玩家掉线时保留会话，重连后补齐历史帧；结束时生成结算结果和回放记录。

这个场景对应实时游戏服务中常见的业务链路：

`入房 -> 准备 -> 开局 -> 帧同步 -> 掉线重连 -> 结算 -> 回放`

## 业务规则

- 房间默认支持 2 至 4 名玩家。
- 只有处于等待状态的房间允许加入和准备。
- 所有玩家准备完成后才能开局。
- 指令必须属于发送者本人，且帧号必须等于服务端当前帧。
- 移动方向取值 0 至 8，非法指令会被拒绝。
- 掉线只改变连接状态，不删除玩家和对局数据。
- 每个帧为每名玩家补齐一条静止指令，保证所有客户端执行同样的输入集合。
- 对局结束后记录胜者、结束帧、结束原因和排行榜。
- 每个已执行帧保存输入和状态校验和，可用于回放或问题复盘。

## 代码落点

- `common/MatchDomain.h`：业务聚合根 `MatchRoom`，不依赖 UDP，便于单元测试。
- `business/main.cpp`：可运行的业务场景演示，模拟 Alice/Bob 对局和一次断线重连。
- `common/GameWorld.h`：继续负责确定性游戏模拟。
- `server/LockstepServer.h`：继续负责 UDP 收发和帧广播。

## 运行

~~~bash
cmake -S . -B build
cmake --build build --config Release --target lockstep_business_demo
build/Release/lockstep_business_demo
~~~

预期输出包含：

- `state=FINISHED`
- `frames=90`
- `reconnect_flow=frame35:offline,frame45:reconnected`
- `deterministic_checksum=...`

业务层与网络层分离后，可以在不启动 UDP 服务的情况下验证房间规则，也方便后续接入数据库、匹配服务或管理后台。


