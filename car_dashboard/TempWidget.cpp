#include "TempWidget.hpp"

#include <QVBoxLayout>
#include <QtGlobal>

TempWidget::TempWidget(const QString& title, const QString& unit,
                       float minVal, float maxVal, QWidget* parent)
    : QWidget(parent), unit_(unit), min_(minVal), max_(maxVal)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(2);

    titleLabel_ = new QLabel(title, this);
    titleLabel_->setStyleSheet("color: #999; font-size: 11px;");
    titleLabel_->setAlignment(Qt::AlignCenter);
    lay->addWidget(titleLabel_);

    bar_ = new QProgressBar(this);
    bar_->setRange(0, 1000);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(14);
    bar_->setStyleSheet(
        "QProgressBar { background: #222; border: 1px solid #444; border-radius: 3px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #1a6fb5, stop:0.5 #3a9bdc, stop:1 #1a6fb5); border-radius: 2px; }");
    lay->addWidget(bar_);

    valueLabel_ = new QLabel("--", this);
    valueLabel_->setStyleSheet("color: white; font-size: 14px; font-weight: bold;");
    valueLabel_->setAlignment(Qt::AlignCenter);
    lay->addWidget(valueLabel_);

    unitLabel_ = new QLabel(unit, this);
    unitLabel_->setStyleSheet("color: #777; font-size: 10px;");
    unitLabel_->setAlignment(Qt::AlignCenter);
    lay->addWidget(unitLabel_);
}

void TempWidget::setValue(float value) {
    value = qBound(min_, value, max_);
    int pct = static_cast<int>((value - min_) / (max_ - min_) * 1000);
    bar_->setValue(pct);

    if (value > max_ * 0.8f) {
        valueLabel_->setStyleSheet("color: #ff4444; font-size: 14px; font-weight: bold;");
        bar_->setStyleSheet(
            "QProgressBar { background: #222; border: 1px solid #444; border-radius: 3px; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 #aa2222, stop:0.5 #cc3333, stop:1 #aa2222); border-radius: 2px; }");
    } else {
        valueLabel_->setStyleSheet("color: white; font-size: 14px; font-weight: bold;");
        bar_->setStyleSheet(
            "QProgressBar { background: #222; border: 1px solid #444; border-radius: 3px; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "  stop:0 #1a6fb5, stop:0.5 #3a9bdc, stop:1 #1a6fb5); border-radius: 2px; }");
    }

    if (unit_ == "%") {
        valueLabel_->setText(QString::number(static_cast<int>(value)) + " " + unit_);
    } else {
        valueLabel_->setText(QString::number(value, 'f', 1) + " " + unit_);
    }
}
