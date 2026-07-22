#pragma once

#include <cstdint>

#include "ShmLayout.hpp"

namespace business {

// 自动落锁:速度 > 20km/h 且未锁时,发一条 lock=1 给 actuator_srv
void autoLock(float speed_kmh, uint8_t lock_status);

// 超速告警:> 120km/h 时周期日志告警(20 帧一次,避免刷屏)
void overspeedWarn(float speed_kmh);

// 拉取 actuator_srv 当前门控状态(GET_ALL),返回 6 位门状态 mask
uint8_t queryDoorMask();

// 拉取门控 mask 并同步到两个 ShmBlock,状态未变则不写
// 返回 true 表示 mask 有变化(已写入)
bool syncDoorMaskToShm(ShmBlock* buf0, ShmBlock* buf1);

} // namespace business