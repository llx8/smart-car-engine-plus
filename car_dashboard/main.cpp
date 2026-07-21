#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <cstdlib>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

#include "SpeedGauge.hpp"
#include "TachGauge.hpp"
#include "TempWidget.hpp"
#include "DoorPanel.hpp"
#include "FaultLamp.hpp"
#include "ShmReader.hpp"
#include <ShmLayout.hpp>
#include <CarMsg.hpp>
#include "DtcListWidget.hpp"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // ── 窗口 ──
    QWidget window;
    window.setWindowTitle("Smart Car Dashboard — M2");
    window.setStyleSheet("background-color: #121212;");
    window.resize(900, 680);

    QVBoxLayout* mainLayout = new QVBoxLayout(&window);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // ═══════════════════════════════════════
    // 第 1 行：速度表 + 转速表
    // ═══════════════════════════════════════
    auto* speedGauge = new SpeedGauge();
    auto* tachGauge  = new TachGauge();
    auto* gaugesRow = new QHBoxLayout();
    gaugesRow->addWidget(speedGauge);
    gaugesRow->addWidget(tachGauge);
    mainLayout->addLayout(gaugesRow);

    // ═══════════════════════════════════════
    // 第 2 行：水温 / 油温 / 燃油 / 电池
    // ═══════════════════════════════════════
    auto* waterTemp  = new TempWidget("水温", "°C", 0, 120);
    auto* oilTemp    = new TempWidget("油温", "°C", 0, 150);
    auto* fuelLevel  = new TempWidget("燃油", "%", 0, 100);
    auto* battery    = new TempWidget("电池", "V", 8, 16);
    auto* sensorsRow = new QHBoxLayout();
    sensorsRow->addWidget(waterTemp);
    sensorsRow->addWidget(oilTemp);
    sensorsRow->addWidget(fuelLevel);
    sensorsRow->addWidget(battery);
    mainLayout->addLayout(sensorsRow);

    // ═══════════════════════════════════════
    // 第 3 行：档位 / 手刹 / 中控锁
    // ═══════════════════════════════════════
    auto* gearLabel     = new QLabel("档位: --");
    auto* handbrakeLabel = new QLabel("手刹: --");
    auto* lockLabel     = new QLabel("中控锁: --");
    for (auto* l : {gearLabel, handbrakeLabel, lockLabel}) {
        l->setStyleSheet("color: white; font-size: 16px; font-weight: bold;"
                         "background: #1e1e1e; border: 1px solid #444;"
                         "border-radius: 4px; padding: 6px 14px;");
    }
    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(gearLabel);
    statusRow->addWidget(handbrakeLabel);
    statusRow->addWidget(lockLabel);
    statusRow->addStretch();
    mainLayout->addLayout(statusRow);

    // ═══════════════════════════════════════
    // 第 4 行：车门状态
    // ═══════════════════════════════════════
    auto* doorPanel = new DoorPanel();
    mainLayout->addWidget(doorPanel);

    // ═══════════════════════════════════════
    // 第 5 行：故障灯阵列
    // ═══════════════════════════════════════
    auto* lampEngine  = new FaultLamp("发动机");
    auto* lampAbs     = new FaultLamp("ABS");
    auto* lampAirbag  = new FaultLamp("气囊");
    auto* lampBattery = new FaultLamp("电池");
    auto* lampsRow = new QHBoxLayout();
    lampsRow->addWidget(lampEngine);
    lampsRow->addWidget(lampAbs);
    lampsRow->addWidget(lampAirbag);
    lampsRow->addWidget(lampBattery);
    lampsRow->addStretch();
    mainLayout->addLayout(lampsRow);

    // ═══════════════════════════════════════
    // 第 6 行：空调控制面板 (Unix socket → car_core → actuator_srv)
    // ═══════════════════════════════════════
    static const char* kBtnStyle =
        "QPushButton { color: white; background: #1e1e1e; border: 1px solid #555;"
        " border-radius: 3px; padding: 4px 10px; font-size: 13px; }"
        "QPushButton:hover { background: #333; border-color: #888; }"
        "QPushButton:pressed { background: #0a0a0a; }";
    static const char* kValLabelStyle =
        "color: #0f0; font-size: 14px; font-weight: bold;"
        "background: #1a1a1a; border: 1px solid #333;"
        "border-radius: 3px; padding: 3px 8px;";
    static const char* kSectionLabelStyle =
        "color: #888; font-size: 14px;";

    auto sendCmd = [](ModId mod, uint8_t item, uint8_t val) -> uint8_t {
        int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (fd < 0) return 1;
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, "/tmp/car_core.sock", sizeof(addr.sun_path) - 1);
        if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd); return 1;
        }
        struct timeval tv = {0, 200000};  // 200ms 防止卡死 UI
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        auto req = makeReq(mod, CmdType::WRITE, item, val);
        send(fd, &req, sizeof(req), MSG_NOSIGNAL);
        CarMsgResp resp{};
        recv(fd, &resp, sizeof(resp), 0);
        close(fd);
        return resp.result;
    };

    auto* acRow = new QHBoxLayout();

    auto* acLabel = new QLabel("空调");
    acLabel->setStyleSheet(kSectionLabelStyle);
    acRow->addWidget(acLabel);

    // AC 开关
    auto* acSwitchBtn  = new QPushButton("AC 关");
    acSwitchBtn->setStyleSheet(kBtnStyle);
    acRow->addWidget(acSwitchBtn);

    // 风量
    auto* fanMinusBtn = new QPushButton("-");
    auto* fanValLabel = new QLabel("3");
    auto* fanPlusBtn  = new QPushButton("+");
    for (auto* b : {fanMinusBtn, fanPlusBtn}) b->setStyleSheet(kBtnStyle);
    fanValLabel->setStyleSheet(kValLabelStyle);
    auto* fanLabel = new QLabel("风量");
    fanLabel->setStyleSheet(kSectionLabelStyle);
    acRow->addWidget(fanLabel);
    acRow->addWidget(fanMinusBtn);
    acRow->addWidget(fanValLabel);
    acRow->addWidget(fanPlusBtn);

    // 温度
    auto* tempMinusBtn = new QPushButton("-");
    auto* tempValLabel = new QLabel("24°C");
    auto* tempPlusBtn  = new QPushButton("+");
    for (auto* b : {tempMinusBtn, tempPlusBtn}) b->setStyleSheet(kBtnStyle);
    tempValLabel->setStyleSheet(kValLabelStyle);
    auto* tempLabel = new QLabel("温度");
    tempLabel->setStyleSheet(kSectionLabelStyle);
    acRow->addWidget(tempLabel);
    acRow->addWidget(tempMinusBtn);
    acRow->addWidget(tempValLabel);
    acRow->addWidget(tempPlusBtn);

    // 循环
    auto* recircBtn = new QPushButton("外循环");
    recircBtn->setStyleSheet(kBtnStyle);
    acRow->addWidget(recircBtn);

    // 锁车
    auto* lockBtn = new QPushButton("锁车");
    lockBtn->setStyleSheet(kBtnStyle);
    acRow->addWidget(lockBtn);

    acRow->addStretch();
    mainLayout->addLayout(acRow);

    // ── 按钮事件 ──
    static int acOn = 0, fan = 3, temp = 24, recirc = 0;
    QObject::connect(acSwitchBtn, &QPushButton::clicked, [&]() {
        acOn = 1 - acOn;
        sendCmd(ModId::AC, ac_item::kAcSwitch, static_cast<uint8_t>(acOn));
        acSwitchBtn->setText(acOn ? "AC 开" : "AC 关");
    });
    QObject::connect(fanMinusBtn, &QPushButton::clicked, [&]() {
        if (fan > 0) { fan--; sendCmd(ModId::AC, ac_item::kFanSpeed, static_cast<uint8_t>(fan)); }
        fanValLabel->setText(QString::number(fan));
    });
    QObject::connect(fanPlusBtn, &QPushButton::clicked, [&]() {
        if (fan < 7) { fan++; sendCmd(ModId::AC, ac_item::kFanSpeed, static_cast<uint8_t>(fan)); }
        fanValLabel->setText(QString::number(fan));
    });
    QObject::connect(tempMinusBtn, &QPushButton::clicked, [&]() {
        if (temp > 16) { temp--; sendCmd(ModId::AC, ac_item::kTargetTemp, static_cast<uint8_t>(temp)); }
        tempValLabel->setText(QString("%1°C").arg(temp));
    });
    QObject::connect(tempPlusBtn, &QPushButton::clicked, [&]() {
        if (temp < 32) { temp++; sendCmd(ModId::AC, ac_item::kTargetTemp, static_cast<uint8_t>(temp)); }
        tempValLabel->setText(QString("%1°C").arg(temp));
    });
    QObject::connect(recircBtn, &QPushButton::clicked, [&]() {
        recirc = 1 - recirc;
        sendCmd(ModId::AC, ac_item::kRecirculation, static_cast<uint8_t>(recirc));
        recircBtn->setText(recirc ? "内循环" : "外循环");
    });
    QObject::connect(lockBtn, &QPushButton::clicked, [&]() {
        sendCmd(ModId::DOOR, door_item::kLock, 1);
    });

    // ═══════════════════════════════════════
    // 第 7 行：DTC 故障列表
    // ═══════════════════════════════════════
    auto* dtcList = new DtcListWidget();
    mainLayout->addWidget(dtcList, 1); // stretch=1，占据剩余空间

    mainLayout->addStretch();

    // ═══════════════════════════════════════
    // 数据源
    // ═══════════════════════════════════════
    ShmReader* shmReader = nullptr;
    if (argc >= 3) {
        int shmid   = std::atoi(argv[1]);
        int eventfd = std::atoi(argv[2]);
shmReader = new ShmReader(shmid, eventfd, &window);
        if (shmReader) {
            QObject::connect(shmReader, &ShmReader::dataUpdated, [&]() {
                const ShmBlock* d = shmReader->data();
                if (!d) return;
                speedGauge->setSpeed(d->speed_kmh);
                tachGauge->setRpm(d->engine_rpm);
                waterTemp->setValue(d->water_temp_c);
                oilTemp->setValue(d->oil_temp_c);
                fuelLevel->setValue(d->fuel_percent);
                battery->setValue(d->battery_voltage);

                const char* gears[] = {"P", "R", "N", "D"};
                gearLabel->setText(QString("档位: %1")
                    .arg(d->gear < 4 ? gears[d->gear] : "?"));
                handbrakeLabel->setText(QString("手刹: %1")
                    .arg(d->hand_brake ? "拉起" : "放下"));
                lockLabel->setText(QString("中控锁: %1")
                    .arg(d->lock_status ? "已锁定" : "已解锁"));

                doorPanel->setDoorMask(d->door_mask);

                lampEngine->setMode((d->fault_lamp_mask >> 0) & 1 ? d->fault_blink[0] : 0);
                lampAbs->setMode((d->fault_lamp_mask >> 1) & 1 ? d->fault_blink[1] : 0);
                lampAirbag->setMode((d->fault_lamp_mask >> 2) & 1 ? d->fault_blink[2] : 0);
                lampBattery->setMode((d->fault_lamp_mask >> 3) & 1 ? d->fault_blink[3] : 0);

                // ── AI 状态提示 ──
                static QLabel* aiLabel = nullptr;
                if (!aiLabel) {
                    aiLabel = new QLabel(&window);
                    aiLabel->setStyleSheet(
                        "color: #ffaa00; font-size: 14px; font-weight: bold;"
                        "background: #332200; border-radius: 4px;"
                        "padding: 4px 12px;");
                    mainLayout->addWidget(aiLabel);
                }
                if (d->ai_status != 0) {
                    aiLabel->setText(QString("⚠ AI 降级: %1").arg(d->ai_message));
                    aiLabel->show();
                } else {
                    aiLabel->hide();
                }
            });

            // ShmHeader.magic + 活跃性检测: 1s 定时器, magic 失效或 3s 未更新 → exit
            auto* aliveCheck = new QTimer(&window);
            static uint32_t last_ri     = 0;
            static uint8_t  stale_count = 0;
            QObject::connect(aliveCheck, &QTimer::timeout, [&]() {
                if (!shmReader) return;
                const auto* hdr = shmReader->header();
                if (!hdr || hdr->magic != 0xCAFE0001) {
                    std::cerr << "[car_dashboard] shm magic invalid, exiting" << std::endl;
                    QApplication::exit(1);
                    return;
                }
                uint32_t ri = shmReader->readIndex();
                if (ri == last_ri && last_ri > 0) {
                    if (++stale_count >= 3) {
                        std::cerr << "[car_dashboard] shm stale for 3s, exiting" << std::endl;
                        QApplication::exit(1);
                    }
                } else {
                    stale_count = 0;
                    last_ri = ri;
                }
            });
            aliveCheck->start(1000);
        }
    } else {
        std::cout << "[car_dashboard] Demo mode" << std::endl;
        auto* demoTimer = new QTimer(&window);
        float demo_speed = 0.0f;
        bool  accel = true;
        int   phase = 0;
        QObject::connect(demoTimer, &QTimer::timeout, [&, accel, phase]() mutable {
            if (accel) {
                demo_speed += 2.5f;
                if (demo_speed >= 80.0f) { accel = false; phase = 0; }
            } else if (phase < 30) {
                phase++; // 巡航
            } else {
                demo_speed -= 1.5f;
                if (demo_speed <= 0.0f) { demo_speed = 0.0f; accel = true; }
            }

            float ratio = demo_speed / 80.0f;
            int rpm = static_cast<int>(ratio * 3000 + 800);
            float wt = 50 + ratio * 40;
            float ot = 70 + ratio * 30;
            float fuel = 100 - ratio * 30;
            float bv = 14.0f - ratio * 1.5f;
            uint8_t gear = (demo_speed < 1) ? 0 : (demo_speed < 30 ? 2 : 3);

            speedGauge->setSpeed(demo_speed);
            tachGauge->setRpm(rpm);
            waterTemp->setValue(wt);
            oilTemp->setValue(ot);
            fuelLevel->setValue(fuel);
            battery->setValue(bv);

            const char* gears[] = {"P", "R", "N", "D"};
            gearLabel->setText(QString("档位: %1").arg(gears[gear]));
            handbrakeLabel->setText(demo_speed < 1 ? "手刹: 拉起" : "手刹: 放下");
            lockLabel->setText(demo_speed > 10 ? "中控锁: 已锁定" : "中控锁: 已解锁");

            uint8_t dmask = (demo_speed > 10) ? 0x3F : 0x00; // 行车时全关
            if (demo_speed < 1) dmask = 0x01; // 停车时左前门开
            doorPanel->setDoorMask(dmask);

            // Demo 故障灯：高速时发动机黄灯
            lampEngine->setMode(demo_speed > 70 ? 2 : 0);
            lampAbs->setMode(0);
            lampAirbag->setMode(0);
            lampBattery->setMode(demo_speed > 75 ? 3 : 0);
        });
        demoTimer->start(50);
    }

    window.show();
    return app.exec();
}
