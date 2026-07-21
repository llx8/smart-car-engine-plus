#include "FaultLamp.hpp"

#include <QPainter>
#include <QTimer>

FaultLamp::FaultLamp(const QString& name, QWidget* parent)
    : QWidget(parent), name_(name)
{
    setFixedSize(48, 48);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this]() {
        blinkOn_ = !blinkOn_;
        update();
    });
}

void FaultLamp::setMode(int mode) {
    mode_ = mode;
    switch (mode) {
    case 0: timer_->stop();  blinkOn_ = false; break;
    case 1: timer_->stop();  blinkOn_ = true;  break;
    case 2: timer_->start(500); break;
    case 3: timer_->start(250); break;
    }
    update();
}

void FaultLamp::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF r = rect().adjusted(4, 4, -4, -4);
    QColor color;

    if (mode_ == 0 || !blinkOn_) {
        color = QColor(30, 120, 30);
    } else {
        color = QColor(200, 30, 30);
    }

    p.setBrush(color);
    p.setPen(QPen(color.darker(150), 2));
    p.drawEllipse(r);

    p.setPen(Qt::white);
    p.setFont(QFont("Arial", 8, QFont::Bold));
    p.drawText(rect(), Qt::AlignCenter, name_);
}
