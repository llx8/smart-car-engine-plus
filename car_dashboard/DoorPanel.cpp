#include "DoorPanel.hpp"

DoorPanel::DoorPanel(QWidget* parent) : QWidget(parent) {
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

void DoorPanel::setDoorMask(uint8_t mask) {
    for (int i = 0; i < 6; ++i) {
        bool open = (mask >> i) & 1;
        lamps_[i]->setStyleSheet(
            QString("background: %1; color: white; border-radius: 4px;"
                    "font-size: 11px; font-weight: bold;")
                .arg(open ? "#aa2222" : "#2d5a1e"));
        lamps_[i]->setText(names_[i] + (open ? "开" : "关"));
    }
}
