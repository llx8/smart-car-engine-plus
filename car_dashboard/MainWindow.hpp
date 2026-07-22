#pragma once

#include <QMainWindow>
#include <cstdint>

#include "CarMsg.hpp"

class QVBoxLayout;
class SpeedGauge;
class TachGauge;
class TempWidget;
class DoorPanel;
class FaultLamp;
class DtcListWidget;
class AcPanel;
class ShmReader;
class QTimer;
class QLabel;
struct ShmBlock;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // shm_id/eventfd_fd >= 0 → 接入 car_core 的实时数据
    // 否则 → 进入 Demo 模式(self-driven 动画)
    explicit MainWindow(int shm_id, int eventfd_fd, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onShmUpdated();
    void onAliveCheck();

private:
    void connectShm(int shm_id, int eventfd_fd);
    void connectDemo();
    void sendActuatorCmd(ModId mod, uint8_t item, uint8_t val);
    void applyShmData(const ShmBlock* d);
    void queryAcState();  // 每 2s 拉 actuator_srv AC 全量状态,同步到 AcPanel

    // 控件
    SpeedGauge*   speed_gauge_     = nullptr;
    TachGauge*    tach_gauge_      = nullptr;
    TempWidget*   water_temp_      = nullptr;
    TempWidget*   oil_temp_        = nullptr;
    TempWidget*   fuel_level_      = nullptr;
    TempWidget*   battery_         = nullptr;
    QLabel*       gear_label_      = nullptr;
    QLabel*       handbrake_label_ = nullptr;
    QLabel*       lock_label_      = nullptr;
    DoorPanel*    door_panel_      = nullptr;
    FaultLamp*    lamp_engine_     = nullptr;
    FaultLamp*    lamp_abs_        = nullptr;
    FaultLamp*    lamp_airbag_     = nullptr;
    FaultLamp*    lamp_battery_    = nullptr;
    AcPanel*      ac_panel_        = nullptr;
    QLabel*       ai_label_        = nullptr;
    DtcListWidget* dtc_list_       = nullptr;
    QVBoxLayout*  main_layout_     = nullptr;

    // 数据源
    ShmReader*    shm_reader_ = nullptr;
    QTimer*       alive_timer_ = nullptr;
    QTimer*       demo_timer_  = nullptr;

    // 活跃性检测
    uint32_t last_ri_        = 0;
    uint8_t  stale_count_   = 0;
    bool     ai_added_       = false;

    // AC 状态定时同步:每 2s 拉 actuator_srv 全量,防止 AI/CLI 改后 UI 不更新
    QTimer* ac_sync_timer_ = nullptr;
};