# Smart Car Engine Plus

基于 CAN 总线的智能车载中控与诊断系统。

## 架构概览

```
CAN 模拟器 → vcan0 → car_core → 共享内存 → Qt 仪表盘
                               → Unix socket → actuator_srv
                               → SQLite (DTC 日志)
```

多进程架构，AF_UNIX SOCK_SEQPACKET 通信，高频仪表数据通过共享内存 + eventfd 推送。

## 里程碑

| 阶段 | 内容 | 状态 |
|------|------|------|
| M1 | CAN 驱动 + 基础仪表盘 | 未开始 |
| M2 | 完整仪表 + DTC 诊断 + AI 语音 | 未开始 |
| M3 | DTC 完整闭环 + 看门狗 | 未开始 |

## 构建

```bash
./scripts/build.sh           # Debug
./scripts/build.sh Release   # Release
```

## 依赖

- CMake 3.16+, C++17
- Qt 5.15+（仪表盘）
- spdlog, SQLite3, nlohmann/json, libcurl
- Linux CAN 模块（vcan0）

## 后续扩展

| 方向 |
|------|
| OTA 固件升级 | 模拟 ECU 远程升级——版本检查→差分下载→校验→A/B 分区切换 |
| 离线语音识别 | 集成 Vosk/Whisper，替换命令行输入，实现车载语音助手 |
| 车云协同 | 车载数据 TLV 编码 → 边缘网关 → MQTT 上云 |
| CAN 硬件验证 | 用 RK3588 CAN 控制器替代 vcan0，示波器抓波形 |
| 多屏联动 | 仪表盘（QPainter 自绘）+ 中控大屏（QML 触控）|
