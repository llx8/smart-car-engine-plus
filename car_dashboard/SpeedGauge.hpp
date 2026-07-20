#pragma once

#include <QWidget>

class SpeedGauge : public QWidget {
    Q_OBJECT
public:
    explicit SpeedGauge(QWidget* parent = nullptr);

    void setSpeed(float kmh);

    QSize minimumSizeHint() const override { return QSize(200, 200); }
    QSize sizeHint() const override { return QSize(300, 300); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float speed_{0};
    static constexpr float kMaxSpeed = 260.0f;

    // 绘制角度：从 225° (左下) 逆时针扫 270° 到 135° (右下)
    static constexpr qreal kStartAngleDeg = 225.0;
    static constexpr qreal kSpanAngleDeg  = 270.0;

    void drawBackground(QPainter& p, const QRectF& rect);
    void drawTicks(QPainter& p, const QRectF& rect);
    void drawPointer(QPainter& p, const QRectF& rect);
    void drawCenterText(QPainter& p, const QRectF& rect);
};
