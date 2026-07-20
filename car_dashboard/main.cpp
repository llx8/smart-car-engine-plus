#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QTimer>
#include <cstdlib>
#include <iostream>

#include "SpeedGauge.hpp"
#include "TachGauge.hpp"
#include "TempWidget.hpp"
#include "DoorPanel.hpp"
#include "FaultLamp.hpp"
#include "ShmReader.hpp"
#include <ShmLayout.hpp>
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
    // 第 6 行：DTC 故障列表
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
