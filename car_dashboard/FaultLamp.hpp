#pragma once

#include <QWidget>
#include <QPainter>
#include <QTimer>

// ── 故障灯控件：自绘圆形，三色 + 闪烁 ──
// 状态: 0=熄灭(绿) 1=常亮(红) 2=慢闪(1Hz 红) 3=快闪(2Hz 红)

class FaultLamp : public QWidget {
    Q_OBJECT
public:
    explicit FaultLamp(const QString& name, QWidget* parent = nullptr)
        : QWidget(parent), name_(name)
    {
        setFixedSize(48, 48);

        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, [this]() {
            blinkOn_ = !blinkOn_;
            update();
        });
    }

    // mode: 0=正常(绿) 1=常亮故障(红) 2=慢闪(1Hz) 3=快闪(2Hz)
    void setMode(int mode) {
        mode_ = mode;
        switch (mode) {
        case 0: timer_->stop();  blinkOn_ = false; break;
        case 1: timer_->stop();  blinkOn_ = true;  break;
        case 2: timer_->start(500); break; // 1Hz = 500ms 半周期
        case 3: timer_->start(250); break; // 2Hz = 250ms 半周期
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRectF r = rect().adjusted(4, 4, -4, -4);
        QColor color;

        if (mode_ == 0 || !blinkOn_) {
            color = QColor(30, 120, 30); // 绿/灭
        } else {
            color = QColor(200, 30, 30); // 红（故障：常亮/慢闪/快闪统一用红）
        }

        p.setBrush(color);
        p.setPen(QPen(color.darker(150), 2));
        p.drawEllipse(r);

        // 标签文字
        p.setPen(Qt::white);
        p.setFont(QFont("Arial", 8, QFont::Bold));
        p.drawText(rect(), Qt::AlignCenter, name_);
    }

private:
    QString name_;
    int mode_ = 0;
    bool blinkOn_ = false;
    QTimer* timer_;
};
