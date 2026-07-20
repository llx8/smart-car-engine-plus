#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <cstdlib>
#include <iostream>

#include "SpeedGauge.hpp"
#include "TachGauge.hpp"
#include "ShmReader.hpp"
#include <ShmLayout.hpp>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // ── 窗口 ──
    QWidget window;
    window.setWindowTitle("Smart Car Dashboard — M1");
    window.setStyleSheet("background-color: #1a1a1a;");
    window.resize(780, 480);

    // ── 布局 ──
    QVBoxLayout* mainLayout = new QVBoxLayout(&window);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // 第一行：速度表 + 转速表
    QHBoxLayout* gaugesLayout = new QHBoxLayout();
    SpeedGauge* speedGauge = new SpeedGauge();
    TachGauge*  tachGauge  = new TachGauge();
    gaugesLayout->addWidget(speedGauge);
    gaugesLayout->addWidget(tachGauge);
    mainLayout->addLayout(gaugesLayout);

    // 第二行：水温
    QHBoxLayout* infoLayout = new QHBoxLayout();
    QLabel* waterLabel = new QLabel("水温");
    waterLabel->setStyleSheet("color: white; font-size: 14px; font-weight: bold;");
    waterLabel->setFixedWidth(50);

    QProgressBar* waterBar = new QProgressBar();
    waterBar->setRange(0, 120);
    waterBar->setValue(0);
    waterBar->setTextVisible(true);
    waterBar->setFormat("%v °C");
    waterBar->setStyleSheet(
        "QProgressBar {"
        "  background-color: #333; border: 1px solid #555; border-radius: 4px;"
        "  color: white; text-align: center; height: 22px;"
        "}"
        "QProgressBar::chunk {"
        "  background-color: #3a9bdc; border-radius: 3px;"
        "}");

    infoLayout->addWidget(waterLabel);
    infoLayout->addWidget(waterBar, 1);
    infoLayout->addStretch(400);
    mainLayout->addLayout(infoLayout);

    mainLayout->addStretch();

    // ── 数据源：命令行参数 → ShmReader / 否则 → Demo 定时器 ──
    ShmReader* shmReader = nullptr;
    if (argc >= 3) {
        // 真实模式：car_core 传入 shmid 和 eventfd
        int shmid     = std::atoi(argv[1]);
        int eventfd   = std::atoi(argv[2]);
        shmReader = new ShmReader(shmid, eventfd, &window);
        if (shmReader) {
            QObject::connect(shmReader, &ShmReader::dataUpdated, [&]() {
                const ShmBlock* data = shmReader->data();
                if (!data) return;
                speedGauge->setSpeed(data->speed_kmh);
                tachGauge->setRpm(data->engine_rpm);
                waterBar->setValue(static_cast<int>(data->water_temp_c));
            });
        }
    } else {
        // Demo 模式：QTimer 模拟行驶数据，方便开发阶段验证仪表绘制效果
        std::cout << "[car_dashboard] Demo mode (no shmid/eventfd args)" << std::endl;
        QTimer* demoTimer = new QTimer(&window);
        float demo_speed = 0.0f;
        bool  accelerating = true;
        QObject::connect(demoTimer, &QTimer::timeout, [&, accelerating]() mutable {
            // 模拟加速→巡航→减速→停车循环
            if (accelerating) {
                demo_speed += 3.0f;
                if (demo_speed >= 80.0f) accelerating = false;
            } else {
                demo_speed -= 2.0f;
                if (demo_speed <= 0.0f) { demo_speed = 0.0f; accelerating = true; }
            }

            int demo_rpm = static_cast<int>((demo_speed / 80.0f) * 3000.0f + 800.0f);
            float water_temp = 50.0f + (demo_speed / 80.0f) * 40.0f; // 50~90°C

            speedGauge->setSpeed(demo_speed);
            tachGauge->setRpm(demo_rpm);
            waterBar->setValue(static_cast<int>(water_temp));
        });
        demoTimer->start(50); // 20Hz
    }

    window.show();
    return app.exec();
}
