#include "SpeedGauge.hpp"
#include <QPainter>
#include <QtMath>

SpeedGauge::SpeedGauge(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 200);
}

void SpeedGauge::setSpeed(float kmh)
{
    speed_ = qBound(0.0f, kmh, kMaxSpeed);
    update(); // 触发 paintEvent，Qt 会合并短时间内多次 update() 调用
}

void SpeedGauge::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing); // 反锯齿，指针和弧线更平滑

    QRectF rect = this->rect().adjusted(5, 5, -5, -5);

    // 分层绘制，顺序决定叠加关系
    drawBackground(painter, rect); // 底层：灰色圆弧
    drawTicks(painter, rect);      // 中层：刻度线 + 数字
    drawPointer(painter, rect);    // 顶层：红色指针
    drawCenterText(painter, rect); // 最顶层：速度数值
}

// ── 底层：背景圆弧 ──
void SpeedGauge::drawBackground(QPainter& p, const QRectF& rect)
{
    p.save();

    qreal radius = qMin(rect.width(), rect.height()) / 2.0 - 30;
    QPointF center = rect.center();
    QRectF arcRect(center.x() - radius, center.y() - radius,
                   2 * radius, 2 * radius);

    // 粗灰色圆弧
    QPen arcPen(QColor(50, 50, 50), 12);
    arcPen.setCapStyle(Qt::FlatCap);
    p.setPen(arcPen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(arcRect, kStartAngleDeg * 16, kSpanAngleDeg * 16);

    p.restore();
}

// ── 中层：刻度线和数字 ──
void SpeedGauge::drawTicks(QPainter& p, const QRectF& rect)
{
    p.save();

    qreal radius = qMin(rect.width(), rect.height()) / 2.0 - 30;
    QPointF center = rect.center();

    // 把坐标原点移到表盘中心，后续旋转都绕中心
    p.translate(center);

    for (int speed = 0; speed <= static_cast<int>(kMaxSpeed); speed += 10) {
        qreal ratio   = speed / kMaxSpeed;
        qreal angleDeg = kStartAngleDeg + ratio * kSpanAngleDeg;
        // Qt: 0° = 3 点方向（正右），CCW 为正。
        // 刻度线指向圆弧（12 点方向 = 90°），所以旋转后要补偿 -90°
        qreal drawDeg = angleDeg - 90.0;

        p.save();
        p.rotate(drawDeg);

        bool isMajor = (speed % 20 == 0);

        // 刻度线从内圈画到外圈
        qreal inner = radius - (isMajor ? 22 : 14);
        qreal outer = radius;
        p.setPen(QPen(Qt::white, isMajor ? 3 : 1, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(0, -inner), QPointF(0, -outer));

        // 主刻度写数字
        if (isMajor) {
            p.setPen(Qt::white);
            QFont font("Arial", 9, QFont::Bold);
            p.setFont(font);
            QRectF labelRect(-20, -(inner - 5), 40, 16);
            p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignBottom,
                       QString::number(speed));
        }

        p.restore();
    }

    p.restore();
}

// ── 顶层：红色指针 ──
void SpeedGauge::drawPointer(QPainter& p, const QRectF& rect)
{
    p.save();

    qreal radius = qMin(rect.width(), rect.height()) / 2.0 - 30;
    QPointF center = rect.center();

    // 计算当前速度对应的角度
    qreal angleDeg = kStartAngleDeg + (speed_ / kMaxSpeed) * kSpanAngleDeg;

    p.translate(center);
    p.rotate(angleDeg - 90.0); // 补偿 Qt 坐标系：0° 在 3 点方向

    // 指针：三角形
    qreal needleLen = radius - 30;
    QPolygonF needle;
    needle << QPointF(-4, 0)
           << QPointF(0, -needleLen)
           << QPointF(4, 0);

    p.setBrush(QColor(220, 30, 30)); // 红色
    p.setPen(Qt::NoPen);
    p.drawPolygon(needle);

    // 中心圆点（指针轴）
    p.setBrush(QColor(180, 20, 20));
    p.drawEllipse(QPointF(0, 0), 8, 8);
    p.setBrush(QColor(60, 60, 60));
    p.drawEllipse(QPointF(0, 0), 4, 4);

    p.restore();
}

// ── 最顶层：速度数值 ──
void SpeedGauge::drawCenterText(QPainter& p, const QRectF& rect)
{
    p.save();

    QPointF center = rect.center();

    // 速度数字
    QFont bigFont("Arial", 28, QFont::Bold);
    p.setFont(bigFont);
    p.setPen(Qt::white);
    QRectF speedRect(center.x() - 60, center.y() + 15, 120, 40);
    p.drawText(speedRect, Qt::AlignHCenter | Qt::AlignTop,
               QString::number(static_cast<int>(speed_)));

    // 单位
    QFont smallFont("Arial", 10);
    p.setFont(smallFont);
    p.setPen(QColor(150, 150, 150));
    QRectF unitRect(center.x() - 60, center.y() + 50, 120, 20);
    p.drawText(unitRect, Qt::AlignHCenter | Qt::AlignTop, "km/h");

    p.restore();
}
