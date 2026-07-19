#include "CanFrame.hpp"
#include <cstdio>      // printf
#include <cstring>     // memset
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

int main() {
    // 需要预先配置 vcan0
    // 运行前执行: sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0

    int sock = create_can_socket(kCanIfName);
    if (sock < 0) {
        fprintf(stderr, "Failed to create CAN socket. Make sure vcan0 is set up.\n");
        return 1;
    }

    printf("can_sim running on %s...\n", kCanIfName);

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

        // 计算联动数据
        int32_t rpm = static_cast<int32_t>(speed * 30.0f + kIdleRpm);
        // 车速 80 → rpm = 80*30 + 800 = 3200
        // 怠速     → rpm = 800

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
            frame = encode_water_oil_frame(85.0f, 92.0f);
            if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
                perror("write water_oil");
            }
        }

        if (frame_cnt % kSlowInterval == 0) {
            frame = encode_fuel_battery_frame(65.0f, 12.4f);
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
            printf("speed=%.0f km/h  rpm=%d  water=85°C  fuel=65%%  gear=D\n", speed, rpm);
            fflush(stdout);
        }

        frame_cnt++;
        usleep(50 * 1000);  // 50ms ≈ 20Hz
    }

    close(sock);
    return 0;
}
