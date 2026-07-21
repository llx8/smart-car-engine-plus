#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>

class TempWidget : public QWidget {
    Q_OBJECT
public:
    TempWidget(const QString& title, const QString& unit,
               float minVal, float maxVal, QWidget* parent = nullptr);

    void setValue(float value);

private:
    QLabel* titleLabel_;
    QProgressBar* bar_;
    QLabel* valueLabel_;
    QLabel* unitLabel_;
    QString unit_;
    float min_, max_;
};
