#include "TachGauge.hpp"
#include <QPainter>

TachGauge::TachGauge(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 200);
}

void TachGauge::setRpm(int rpm)
{
    rpm_ = qBound(0, rpm, kMaxRpm);
    update();
}

void TachGauge::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF rect = this->rect().adjusted(5, 5, -5, -5);

    drawBackground(painter, rect);
    drawTicks(painter, rect);
    drawPointer(painter, rect);
    drawCenterText(painter, rect);
}

void TachGauge::drawBackground(QPainter& p, const QRectF& rect)
{
    p.save();
    qreal radius = qMin(rect.width(), rect.height()) / 2.0 - 30;
    QPointF center = rect.center();
    QRectF arcRect(center.x() - radius, center.y() - radius,
                   2 * radius, 2 * radius);
    QPen arcPen(QColor(50, 50, 50), 12);
    arcPen.setCapStyle(Qt::FlatCap);
    p.setPen(arcPen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(arcRect, kStartAngleDeg * 16, kSpanAngleDeg * 16);
    p.restore();
}

void TachGauge::drawTicks(QPainter& p, const QRectF& rect)
{
    p.save();
    qreal radius = qMin(rect.width(), rect.height()) / 2.0 - 30;
    QPointF center = rect.center();
    p.translate(center);

    // 转速表刻度间距：每 500 rpm 一个主刻度，每 250 rpm 一个次刻度
    for (int rpm = 0; rpm <= kMaxRpm; rpm += 250) {
        qreal ratio    = static_cast<qreal>(rpm) / kMaxRpm;
        qreal angleDeg = kStartAngleDeg + ratio * kSpanAngleDeg;
        qreal drawDeg  = angleDeg - 90.0;

        p.save();
        p.rotate(drawDeg);

        bool isMajor = (rpm % 1000 == 0);
        // 500 rpm 也用稍长的刻度（半主刻度）
        bool isSemiMajor = (!isMajor && rpm % 500 == 0);

        qreal inner = radius - (isMajor ? 22 : (isSemiMajor ? 18 : 14));
        qreal outer = radius;
        int penWidth = isMajor ? 3 : (isSemiMajor ? 2 : 1);
        p.setPen(QPen(Qt::white, penWidth, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(0, -inner), QPointF(0, -outer));

        // 主刻度标数字（每 1000 rpm），格式化为 "1", "2", ... 即千转
        if (isMajor) {
            p.setPen(Qt::white);
            QFont font("Arial", 9, QFont::Bold);
            p.setFont(font);
            QRectF labelRect(-20, -(inner - 5), 40, 16);
            // 显示 ×1000 rpm，如 1、2、...、8
            p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignBottom,
                       QString::number(rpm / 1000));
        }

        p.restore();
    }

    p.restore();
}

void TachGauge::drawPointer(QPainter& p, const QRectF& rect)
{
    p.save();
    qreal radius = qMin(rect.width(), rect.height()) / 2.0 - 30;
    QPointF center = rect.center();
    qreal angleDeg = kStartAngleDeg + (rpm_ / static_cast<qreal>(kMaxRpm)) * kSpanAngleDeg;

    p.translate(center);
    p.rotate(angleDeg - 90.0);

    qreal needleLen = radius - 30;
    QPolygonF needle;
    needle << QPointF(-4, 0) << QPointF(0, -needleLen) << QPointF(4, 0);

    p.setBrush(QColor(220, 30, 30));
    p.setPen(Qt::NoPen);
    p.drawPolygon(needle);

    p.setBrush(QColor(180, 20, 20));
    p.drawEllipse(QPointF(0, 0), 8, 8);
    p.setBrush(QColor(60, 60, 60));
    p.drawEllipse(QPointF(0, 0), 4, 4);

    p.restore();
}

void TachGauge::drawCenterText(QPainter& p, const QRectF& rect)
{
    p.save();
    QPointF center = rect.center();

    // 转速数字（如 "2100"）
    QFont bigFont("Arial", 24, QFont::Bold);
    p.setFont(bigFont);
    p.setPen(Qt::white);
    QRectF rpmRect(center.x() - 65, center.y() + 15, 130, 40);
    p.drawText(rpmRect, Qt::AlignHCenter | Qt::AlignTop,
               QString::number(rpm_));

    // 单位
    QFont smallFont("Arial", 10);
    p.setFont(smallFont);
    p.setPen(QColor(150, 150, 150));
    QRectF unitRect(center.x() - 65, center.y() + 50, 130, 20);
    p.drawText(unitRect, Qt::AlignHCenter | Qt::AlignTop, "rpm");

    p.restore();
}
