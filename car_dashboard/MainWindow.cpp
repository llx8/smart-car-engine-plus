#include "MainWindow.hpp"

#include "AcPanel.hpp"
#include "DoorPanel.hpp"
#include "DtcListWidget.hpp"
#include "FaultLamp.hpp"
#include "ShmReader.hpp"
#include "SpeedGauge.hpp"
#include "TachGauge.hpp"
#include "TempWidget.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include <CarMsg.hpp>
#include <ShmLayout.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

MainWindow::MainWindow(int shm_id, int eventfd_fd, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Smart Car Dashboard — M2");
    setStyleSheet("background-color: #121212;");
    resize(900, 680);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    main_layout_ = new QVBoxLayout(central);
    main_layout_->setContentsMargins(8, 8, 8, 8);
    main_layout_->setSpacing(6);

    // 第 1 行:速度表 + 转速表
    speed_gauge_ = new SpeedGauge();
    tach_gauge_  = new TachGauge();
    auto* gaugesRow = new QHBoxLayout();
    gaugesRow->addWidget(speed_gauge_);
    gaugesRow->addWidget(tach_gauge_);
    main_layout_->addLayout(gaugesRow);

    // 第 2 行:水温 / 油温 / 燃油 / 电池
    water_temp_ = new TempWidget("水温", "°C",   0, 120);
    oil_temp_   = new TempWidget("油温", "°C",   0, 150);
    fuel_level_ = new TempWidget("燃油", "%",    0, 100);
    battery_    = new TempWidget("电池", "V",    8, 16);
    auto* sensorsRow = new QHBoxLayout();
    sensorsRow->addWidget(water_temp_);
    sensorsRow->addWidget(oil_temp_);
    sensorsRow->addWidget(fuel_level_);
    sensorsRow->addWidget(battery_);
    main_layout_->addLayout(sensorsRow);

    // 第 3 行:档位 / 手刹 / 中控锁
    gear_label_      = new QLabel("档位: --");
    handbrake_label_ = new QLabel("手刹: --");
    lock_label_      = new QLabel("中控锁: --");
    for (auto* l : {gear_label_, handbrake_label_, lock_label_}) {
        l->setStyleSheet("color: white; font-size: 16px; font-weight: bold;"
                         "background: #1e1e1e; border: 1px solid #444;"
                         "border-radius: 4px; padding: 6px 14px;");
    }
    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(gear_label_);
    statusRow->addWidget(handbrake_label_);
    statusRow->addWidget(lock_label_);
    statusRow->addStretch();
    main_layout_->addLayout(statusRow);

    // 第 4 行:车门状态
    door_panel_ = new DoorPanel();
    main_layout_->addWidget(door_panel_);

    // 第 5 行:故障灯阵列
    lamp_engine_  = new FaultLamp("发动机");
    lamp_abs_     = new FaultLamp("ABS");
    lamp_airbag_  = new FaultLamp("气囊");
    lamp_battery_ = new FaultLamp("电池");
    auto* lampsRow = new QHBoxLayout();
    lampsRow->addWidget(lamp_engine_);
    lampsRow->addWidget(lamp_abs_);
    lampsRow->addWidget(lamp_airbag_);
    lampsRow->addWidget(lamp_battery_);
    lampsRow->addStretch();
    main_layout_->addLayout(lampsRow);

    // 第 6 行:空调面板
    ac_panel_ = new AcPanel();
    main_layout_->addWidget(ac_panel_);
    connect(ac_panel_, &AcPanel::sendCmd, this, &MainWindow::sendActuatorCmd);

    // 第 7 行:DTC 故障列表
    dtc_list_ = new DtcListWidget();
    main_layout_->addWidget(dtc_list_, 1);
    main_layout_->addStretch();

    // AI 降级提示 label (首次出现时才 addWidget 进 layout)
    ai_label_ = new QLabel(central);
    ai_label_->setStyleSheet(
        "color: #ffaa00; font-size: 14px; font-weight: bold;"
        "background: #332200; border-radius: 4px; padding: 4px 12px;");
    ai_label_->hide();

    // 接入数据源:有 shm 参数 → 实时,否则 → Demo
    if (shm_id >= 0 && eventfd_fd >= 0) {
        connectShm(shm_id, eventfd_fd);
    } else {
        std::cout << "[car_dashboard] Demo mode" << std::endl;
        connectDemo();
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::connectShm(int shm_id, int eventfd_fd) {
    shm_reader_ = new ShmReader(shm_id, eventfd_fd, this);
    connect(shm_reader_, &ShmReader::dataUpdated, this, &MainWindow::onShmUpdated);

    // ShmHeader.magic + 活跃性检测:1s 定时器, magic 失效或 3s 未更新 → exit
    alive_timer_ = new QTimer(this);
    connect(alive_timer_, &QTimer::timeout, this, &MainWindow::onAliveCheck);
    alive_timer_->start(1000);

    // AC 状态同步:每 2s 拉 actuator_srv 全量,UI 显示项与 actuator 真值对齐
    ac_sync_timer_ = new QTimer(this);
    connect(ac_sync_timer_, &QTimer::timeout, this, &MainWindow::queryAcState);
    ac_sync_timer_->start(2000);
    queryAcState();  // 首帧立刻拉一次
}

void MainWindow::connectDemo() {
    demo_timer_ = new QTimer(this);
    static float demo_speed = 0.0f;
    static bool  accel      = true;
    static int   phase      = 0;
    connect(demo_timer_, &QTimer::timeout, [=]() mutable {
        if (accel) {
            demo_speed += 2.5f;
            if (demo_speed >= 80.0f) { accel = false; phase = 0; }
        } else if (phase < 30) {
            ++phase; // 巡航
        } else {
            demo_speed -= 1.5f;
            if (demo_speed <= 0.0f) { demo_speed = 0.0f; accel = true; }
        }

        float ratio = demo_speed / 80.0f;
        speed_gauge_->setSpeed(demo_speed);
        tach_gauge_->setRpm(static_cast<int>(ratio * 3000 + 800));
        water_temp_->setValue(50 + ratio * 40);
        oil_temp_->setValue(70 + ratio * 30);
        fuel_level_->setValue(100 - ratio * 30);
        battery_->setValue(14.0f - ratio * 1.5f);

        static const char* gears[] = {"P", "R", "N", "D"};
        uint8_t gear = (demo_speed < 1) ? 0 : (demo_speed < 30 ? 2 : 3);
        gear_label_->setText(QString("档位: %1").arg(gears[gear]));
        handbrake_label_->setText(demo_speed < 1 ? "手刹: 拉起" : "手刹: 放下");
        lock_label_->setText(demo_speed > 10 ? "中控锁: 已锁定" : "中控锁: 已解锁");

        uint8_t dmask = (demo_speed > 10) ? 0x3F : 0x00;
        if (demo_speed < 1) dmask = 0x01; // 停车左前门开
        door_panel_->setDoorMask(dmask);

        lamp_engine_->setMode(demo_speed > 70 ? 2 : 0);
        lamp_abs_->setMode(0);
        lamp_airbag_->setMode(0);
        lamp_battery_->setMode(demo_speed > 75 ? 3 : 0);
    });
    demo_timer_->start(50);
}

void MainWindow::onShmUpdated() {
    const ShmBlock* d = shm_reader_ ? shm_reader_->data() : nullptr;
    if (d) applyShmData(d);

    // AI 降级提示:首次出现时 addWidget,之后用 show/hide 切换
    if (d && d->ai_status != 0) {
        if (!ai_added_) {
            main_layout_->addWidget(ai_label_);
            ai_added_ = true;
        }
        ai_label_->setText(QString("⚠ AI 降级: %1").arg(d->ai_message));
        ai_label_->show();
    } else {
        ai_label_->hide();
    }
}

void MainWindow::onAliveCheck() {
    if (!shm_reader_) return;
    const auto* hdr = shm_reader_->header();
    if (!hdr || hdr->magic != 0xCAFE0001) {
        std::cerr << "[car_dashboard] shm magic invalid, exiting" << std::endl;
        QApplication::exit(1);
        return;
    }
    uint32_t ri = shm_reader_->readIndex();
    if (ri == last_ri_ && last_ri_ > 0) {
        if (++stale_count_ >= 3) {
            std::cerr << "[car_dashboard] shm stale for 3s, exiting" << std::endl;
            QApplication::exit(1);
        }
    } else {
        stale_count_ = 0;
        last_ri_ = ri;
    }
}

void MainWindow::sendActuatorCmd(ModId mod, uint8_t item, uint8_t val) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "/tmp/car_core.sock", sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return;
    }
    struct timeval tv{0, 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    auto req = makeReq(mod, CmdType::WRITE, item, val);
    send(fd, &req, sizeof(req), MSG_NOSIGNAL);
    CarMsgResp resp{};
    recv(fd, &resp, sizeof(resp), 0);
    ::close(fd);
}

void MainWindow::queryAcState() {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "/tmp/car_core.sock", sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return;
    }
    struct timeval tv{0, 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    auto req = makeReq(ModId::AC, CmdType::GET_ALL, 0);
    send(fd, &req, sizeof(req), MSG_NOSIGNAL);
    CarMsgResp resp{};
    if (recv(fd, &resp, sizeof(resp), 0) == sizeof(resp)
        && resp.result == static_cast<uint8_t>(ResultCode::OK)) {
        ac_panel_->syncState(resp.value[0], resp.value[1],
                             resp.value[2], resp.value[3]);
    }
    ::close(fd);
}

void MainWindow::applyShmData(const ShmBlock* d) {
    speed_gauge_->setSpeed(d->speed_kmh);
    tach_gauge_->setRpm(d->engine_rpm);
    water_temp_->setValue(d->water_temp_c);
    oil_temp_->setValue(d->oil_temp_c);
    fuel_level_->setValue(d->fuel_percent);
    battery_->setValue(d->battery_voltage);

    static const char* gears[] = {"P", "R", "N", "D"};
    gear_label_->setText(QString("档位: %1")
        .arg(d->gear < 4 ? gears[d->gear] : "?"));
    handbrake_label_->setText(QString("手刹: %1")
        .arg(d->hand_brake ? "拉起" : "放下"));
    lock_label_->setText(QString("中控锁: %1")
        .arg(d->lock_status ? "已锁定" : "已解锁"));

    door_panel_->setDoorMask(d->door_mask);

    lamp_engine_->setMode ((d->fault_lamp_mask >> 0) & 1 ? d->fault_blink[0] : 0);
    lamp_abs_->setMode    ((d->fault_lamp_mask >> 1) & 1 ? d->fault_blink[1] : 0);
    lamp_airbag_->setMode ((d->fault_lamp_mask >> 2) & 1 ? d->fault_blink[2] : 0);
    lamp_battery_->setMode ((d->fault_lamp_mask >> 3) & 1 ? d->fault_blink[3] : 0);
}