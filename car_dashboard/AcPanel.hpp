#pragma once

#include <QLabel>
#include <QPushButton>
#include <QWidget>

enum class ModId : uint8_t;

// 空调面板控件:AC 开关 / 风量 / 温度 / 内外循环 / 锁车
// 内部只发 sendCmd 信号,不直接碰 socket — socket 通信由 MainWindow 承担
class AcPanel : public QWidget {
    Q_OBJECT
public:
    explicit AcPanel(QWidget* parent = nullptr);

    // 同步外部改动的 AC 状态到本地标签(解决 AI/CLI 改 actuator_srv 后 UI 不更新)
    void syncState(uint8_t ac_on, uint8_t fan, uint8_t temp, uint8_t recirc);

signals:
    void sendCmd(ModId mod, uint8_t item, uint8_t val);

private slots:
    void onAcSwitch();
    void onFanMinus();
    void onFanPlus();
    void onTempMinus();
    void onTempPlus();
    void onRecirc();
    void onLock();

private:
    QPushButton* ac_switch_btn_;
    QLabel*      fan_val_label_;
    QLabel*      temp_val_label_;
    QPushButton* recirc_btn_;

    int ac_on_   = 0;
    int fan_     = 3;
    int temp_    = 24;
    int recirc_  = 0;
};