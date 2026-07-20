#pragma once

#include <QWidget>

class TachGauge : public QWidget {
    Q_OBJECT
public:
    explicit TachGauge(QWidget* parent = nullptr);

    void setRpm(int rpm);

    QSize minimumSizeHint() const override { return QSize(200, 200); }
    QSize sizeHint() const override { return QSize(300, 300); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int rpm_{0};
    static constexpr int kMaxRpm = 8000;

    static constexpr qreal kStartAngleDeg = 225.0;
    static constexpr qreal kSpanAngleDeg  = 270.0;

    void drawBackground(QPainter& p, const QRectF& rect);
    void drawTicks(QPainter& p, const QRectF& rect);
    void drawPointer(QPainter& p, const QRectF& rect);
    void drawCenterText(QPainter& p, const QRectF& rect);
};
