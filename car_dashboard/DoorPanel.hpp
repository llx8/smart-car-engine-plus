#pragma once

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <vector>

// ── 车门状态面板：6 个指示灯（位掩码解析） ──
// door_mask bit0=左前 bit1=右前 bit2=左后 bit3=右后 bit4=后备箱 bit5=天窗

class DoorPanel : public QWidget {
    Q_OBJECT
public:
    explicit DoorPanel(QWidget* parent = nullptr) : QWidget(parent) {
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(10);

        names_ = {"左前", "右前", "左后", "右后", "后备箱", "天窗"};
        for (int i = 0; i < 6; ++i) {
            auto* label = new QLabel(names_[i] + "关", this);
            label->setAlignment(Qt::AlignCenter);
            label->setFixedSize(64, 36);
            label->setStyleSheet(
                "background: #2d5a1e; color: white; border-radius: 4px;"
                "font-size: 11px; font-weight: bold;");
            lay->addWidget(label);
            lamps_.push_back(label);
        }
    }

    void setDoorMask(uint8_t mask) {
        for (int i = 0; i < 6; ++i) {
            bool open = (mask >> i) & 1;
            lamps_[i]->setStyleSheet(
                QString("background: %1; color: white; border-radius: 4px;"
                        "font-size: 11px; font-weight: bold;")
                    .arg(open ? "#aa2222" : "#2d5a1e"));
            lamps_[i]->setText(names_[i] + (open ? "开" : "关"));
        }
    }

private:
    std::vector<QLabel*> lamps_;
    std::vector<QString> names_;
};
