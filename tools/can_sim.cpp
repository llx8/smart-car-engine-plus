#include "CanFrame.hpp"
#include <cstdio>      // printf
#include <cstring>     // memset
#include <cstdlib>     // atoi
#include <cmath>       // sinf
#include <unistd.h>    // usleep, close
#include <sys/socket.h>// socket, bind
#include <net/if.h>    // ifreq
#include <sys/ioctl.h> // ioctl
#include <linux/can.h> // can_frame

// 行驶模拟参数

constexpr const char* kCanIfName = "vcan0";
constexpr int   kMaxSpeed       = 80;       // 最高车速 km/h
constexpr int   kIdleRpm        = 800;      // 怠速转速
constexpr float kAccelRate      = 0.5f;     // 每帧加速度 (km/h)
constexpr float kDecelRate      = 1.0f;     // 每帧减速度 (km/h)
constexpr int   kCruiseFrames   = 60;       // 巡航持续帧数 (60帧×50ms=3s)
constexpr int   kStopFrames     = 20;       // 停车持续帧数 (20帧×50ms=1s)

// 工具函数：创建 CAN socket

static int create_can_socket(const char* ifname) {
    int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl");
        close(sock);
        return -1;
    }

    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    return sock;
}

// 主函数
// 用法: can_sim [fps]
//   默认 20 Hz (50ms 间隔) — 模拟正常工况
//   500 Hz (2ms 间隔)    — 压测模式,验证 car_core 不丢帧
int main(int argc, char* argv[]) {
    // 需要预先配置 vcan0
    // 运行前执行: sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0

    int fps = 20;
    if (argc >= 2) {
        fps = atoi(argv[1]);
        if (fps <= 0) fps = 20;
    }
    const int interval_us = 1000000 / fps;
    printf("can_sim running on %s @ %d Hz (interval=%d us)...\n",
           kCanIfName, fps, interval_us);

    int sock = create_can_socket(kCanIfName);
    if (sock < 0) {
        fprintf(stderr, "Failed to create CAN socket. Make sure vcan0 is set up.\n");
        return 1;
    }

    // 行驶状态
    float  speed     = 0.0f;       // 当前车速
    bool   accel_dir= true;        // true=加速 false=减速
    int    phase_cnt= 0;           // 巡航/停车计数器
    int    frame_cnt = 0;          // 总帧数计数器

    // 定时发送用计数器
    // 20Hz 主循环（50ms），每 tick 收到一帧
    // 水温油温 5Hz → 每 4 tick 发一次
    // 燃油电池 + 档位 2Hz → 每 10 tick 发一次
    constexpr int kWaterOilInterval = 4;    // 200ms
    constexpr int kSlowInterval     = 10;   // 500ms

    while (true) {
        // 更新车速（加速/巡航/减速/停车）
        if (speed <= 0.0f && !accel_dir) {
            // 停车阶段
            phase_cnt++;
            if (phase_cnt >= kStopFrames) {
                accel_dir = true;   // 停车结束，开始加速
                phase_cnt = 0;
            }
        } else if (speed >= kMaxSpeed && accel_dir) {
            // 达到最高速，进入巡航
            phase_cnt++;
            if (phase_cnt >= kCruiseFrames) {
                accel_dir = false;  // 巡航结束，开始减速
                phase_cnt = 0;
            }
        } else if (accel_dir) {
            speed += kAccelRate;
            if (speed > kMaxSpeed) speed = static_cast<float>(kMaxSpeed);
        } else {
            speed -= kDecelRate;
            if (speed < 0.0f) speed = 0.0f;
        }

        // ── 故障注入：30 秒一个周期,4 个故障各 5 秒,后 10 秒正常 ──
        // 0-4s: 发动机(P0115 水温过高)    5-9s: ABS(C0035 高速高转速)
        // 10-14s: 气囊(B0020 超速)       15-19s: 电池(P0560 电压低)
        // 20-29s: 全部正常
        int phase = (frame_cnt / 20) % 30;    // 20Hz → 每秒 20 帧
        bool fault_engine = (phase < 5);
        bool fault_abs    = (phase >= 5  && phase < 10);
        bool fault_airbag = (phase >= 10 && phase < 15);
        bool fault_batt   = (phase >= 15 && phase < 20);

        // 计算联动数据
        int32_t rpm = static_cast<int32_t>(speed * 30.0f + kIdleRpm);

        // 故障相位 override: ABS 需要高速 + 高转速
        if (fault_abs)    { speed = 60.0f; rpm = 4500; }
        // 气囊: 超速近碰撞
        if (fault_airbag) { speed = 75.0f; }

        // 水温: 怠速 78°C, 高速 ~96°C, 随车速线性温升
        float water = 78.0f + speed * 0.22f;
        // 油温: 跟水温联动 + 几度
        float oil   = water + 7.0f;
        // 燃油: 每秒 -0.5% 缓慢下降(演示用,150s 跑空一次循环)
        float fuel  = 80.0f - (frame_cnt * 0.025f);
        if (fuel < 5.0f) fuel = 80.0f;
        // 电瓶: 12.4V + 0.15V 呼吸抖动
        float batt  = 12.4f + 0.15f * sinf(frame_cnt * 0.1f);

        if (fault_engine) { water = 118.0f; oil = 138.0f; } // 水温>110 → P0115
        if (fault_batt)   { batt  = 9.5f; }                 // 电压<11  → P0560

        // ── 发送高频帧（每帧都发） ──
        struct can_frame frame;

        frame = encode_speed_frame(speed);
        if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
            perror("write speed");
        }

        frame = encode_rpm_frame(rpm);
        if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
            perror("write rpm");
        }

        // ── 发送中低频帧 ──
        if (frame_cnt % kWaterOilInterval == 0) {
            frame = encode_water_oil_frame(water, oil);
            if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
                perror("write water_oil");
            }
        }

        if (frame_cnt % kSlowInterval == 0) {
            frame = encode_fuel_battery_frame(fuel, batt);
            if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
                perror("write fuel_battery");
            }

            frame = encode_gear_frame(3, 0, 1);  // D档, 手刹关, 锁车
            if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
                perror("write gear");
            }
        }

        // ── 调试打印 ──
        if (frame_cnt % 20 == 0) {  // 每秒打印一次
            const char* flt = fault_engine ? "P0115-发动机" :
                              fault_abs    ? "C0035-ABS" :
                              fault_airbag ? "B0020-气囊" :
                              fault_batt   ? "P0560-电池" : "";
            char tag[32]{};
            if (flt[0]) std::snprintf(tag, sizeof(tag), "  [FLT: %s]", flt);
            printf("speed=%.0f  rpm=%d  water=%.1fC  oil=%.1fC  fuel=%.1f%%  batt=%.2fV%s\n",
                   speed, rpm, water, oil, fuel, batt, tag);
            fflush(stdout);
        }

        frame_cnt++;
        usleep(interval_us);
    }

    close(sock);
    return 0;
}
