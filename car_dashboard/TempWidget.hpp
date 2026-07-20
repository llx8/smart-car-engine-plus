#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

// ── 通用仪表控件：标题 + 进度条 + 数值 ──
// 复用于水温、油温、燃油、电池电压

class TempWidget : public QWidget {
    Q_OBJECT
public:
    TempWidget(const QString& title, const QString& unit,
               float minVal, float maxVal, QWidget* parent = nullptr)
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
        bar_->setRange(0, 1000); // 千分比精度
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

    void setValue(float value) {
        value = qBound(min_, value, max_);
        int pct = static_cast<int>((value - min_) / (max_ - min_) * 1000);
        bar_->setValue(pct);

        // 水温高时变色
        if (value > max_ * 0.8f) {
            valueLabel_->setStyleSheet("color: #ff4444; font-size: 14px; font-weight: bold;");
            bar_->setStyleSheet(bar_->styleSheet().replace("#3a9bdc", "#cc3333"));
        } else {
            valueLabel_->setStyleSheet("color: white; font-size: 14px; font-weight: bold;");
        }

        // 格式化显示
        if (unit_ == "%") {
            valueLabel_->setText(QString::number(static_cast<int>(value)) + " " + unit_);
        } else {
            valueLabel_->setText(QString::number(value, 'f', 1) + " " + unit_);
        }
    }

private:
    QLabel* titleLabel_;
    QProgressBar* bar_;
    QLabel* valueLabel_;
    QLabel* unitLabel_;
    QString unit_;
    float min_, max_;
};
