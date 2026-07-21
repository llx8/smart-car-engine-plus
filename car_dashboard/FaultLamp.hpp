#pragma once

#include <QWidget>

class FaultLamp : public QWidget {
    Q_OBJECT
public:
    explicit FaultLamp(const QString& name, QWidget* parent = nullptr);

    void setMode(int mode);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString name_;
    int mode_ = 0;
    bool blinkOn_ = false;
    QTimer* timer_;
};
