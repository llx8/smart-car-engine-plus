#include "AcPanel.hpp"

#include <QHBoxLayout>

#include <CarMsg.hpp>

namespace {
constexpr const char* kBtnStyle =
    "QPushButton { color: white; background: #1e1e1e; border: 1px solid #555;"
    " border-radius: 3px; padding: 4px 10px; font-size: 13px; }"
    "QPushButton:hover { background: #333; border-color: #888; }"
    "QPushButton:pressed { background: #0a0a0a; }";

constexpr const char* kValStyle =
    "color: #0f0; font-size: 14px; font-weight: bold;"
    "background: #1a1a1a; border: 1px solid #333;"
    "border-radius: 3px; padding: 3px 8px;";

constexpr const char* kLabelStyle = "color: #888; font-size: 14px;";
} // namespace

AcPanel::AcPanel(QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);

    auto* label = new QLabel("空调", this);
    label->setStyleSheet(kLabelStyle);
    row->addWidget(label);

    ac_switch_btn_ = new QPushButton("AC 关", this);
    ac_switch_btn_->setStyleSheet(kBtnStyle);
    row->addWidget(ac_switch_btn_);

    auto* fanLabel = new QLabel("风量", this);
    fanLabel->setStyleSheet(kLabelStyle);
    row->addWidget(fanLabel);
    auto* fanMinus = new QPushButton("-", this);
    fanMinus->setStyleSheet(kBtnStyle);
    fan_val_label_ = new QLabel("3", this);
    fan_val_label_->setStyleSheet(kValStyle);
    auto* fanPlus = new QPushButton("+", this);
    fanPlus->setStyleSheet(kBtnStyle);
    row->addWidget(fanMinus);
    row->addWidget(fan_val_label_);
    row->addWidget(fanPlus);

    auto* tempLabel = new QLabel("温度", this);
    tempLabel->setStyleSheet(kLabelStyle);
    row->addWidget(tempLabel);
    auto* tempMinus = new QPushButton("-", this);
    tempMinus->setStyleSheet(kBtnStyle);
    temp_val_label_ = new QLabel("24°C", this);
    temp_val_label_->setStyleSheet(kValStyle);
    auto* tempPlus = new QPushButton("+", this);
    tempPlus->setStyleSheet(kBtnStyle);
    row->addWidget(tempMinus);
    row->addWidget(temp_val_label_);
    row->addWidget(tempPlus);

    recirc_btn_ = new QPushButton("外循环", this);
    recirc_btn_->setStyleSheet(kBtnStyle);
    row->addWidget(recirc_btn_);

    auto* lockBtn = new QPushButton("锁车", this);
    lockBtn->setStyleSheet(kBtnStyle);
    row->addWidget(lockBtn);

    row->addStretch();

    connect(ac_switch_btn_, &QPushButton::clicked, this, &AcPanel::onAcSwitch);
    connect(fanMinus,        &QPushButton::clicked, this, &AcPanel::onFanMinus);
    connect(fanPlus,         &QPushButton::clicked, this, &AcPanel::onFanPlus);
    connect(tempMinus,       &QPushButton::clicked, this, &AcPanel::onTempMinus);
    connect(tempPlus,        &QPushButton::clicked, this, &AcPanel::onTempPlus);
    connect(recirc_btn_,     &QPushButton::clicked, this, &AcPanel::onRecirc);
    connect(lockBtn,         &QPushButton::clicked, this, &AcPanel::onLock);
}

void AcPanel::onAcSwitch() {
    ac_on_ = 1 - ac_on_;
    emit sendCmd(ModId::AC, ac_item::kAcSwitch, static_cast<uint8_t>(ac_on_));
    ac_switch_btn_->setText(ac_on_ ? "AC 开" : "AC 关");
}

void AcPanel::onFanMinus() {
    if (fan_ > 0) {
        --fan_;
        emit sendCmd(ModId::AC, ac_item::kFanSpeed, static_cast<uint8_t>(fan_));
    }
    fan_val_label_->setText(QString::number(fan_));
}

void AcPanel::onFanPlus() {
    if (fan_ < 7) {
        ++fan_;
        emit sendCmd(ModId::AC, ac_item::kFanSpeed, static_cast<uint8_t>(fan_));
    }
    fan_val_label_->setText(QString::number(fan_));
}

void AcPanel::onTempMinus() {
    if (temp_ > 16) {
        --temp_;
        emit sendCmd(ModId::AC, ac_item::kTargetTemp, static_cast<uint8_t>(temp_));
    }
    temp_val_label_->setText(QString("%1°C").arg(temp_));
}

void AcPanel::onTempPlus() {
    if (temp_ < 32) {
        ++temp_;
        emit sendCmd(ModId::AC, ac_item::kTargetTemp, static_cast<uint8_t>(temp_));
    }
    temp_val_label_->setText(QString("%1°C").arg(temp_));
}

void AcPanel::onRecirc() {
    recirc_ = 1 - recirc_;
    emit sendCmd(ModId::AC, ac_item::kRecirculation, static_cast<uint8_t>(recirc_));
    recirc_btn_->setText(recirc_ ? "内循环" : "外循环");
}

void AcPanel::onLock() {
    emit sendCmd(ModId::DOOR, door_item::kLock, 1);
}

void AcPanel::syncState(uint8_t ac_on, uint8_t fan, uint8_t temp, uint8_t recirc) {
    ac_on_  = ac_on;
    fan_    = fan;
    temp_   = temp;
    recirc_ = recirc;

    ac_switch_btn_->setText(ac_on_ ? "AC 开" : "AC 关");
    fan_val_label_->setText(QString::number(fan_));
    temp_val_label_->setText(QString("%1°C").arg(temp_));
    recirc_btn_->setText(recirc_ ? "内循环" : "外循环");
}