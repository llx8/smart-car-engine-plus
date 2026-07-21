#pragma once

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <vector>

class DoorPanel : public QWidget {
    Q_OBJECT
public:
    explicit DoorPanel(QWidget* parent = nullptr);

    void setDoorMask(uint8_t mask);

private:
    std::vector<QLabel*> lamps_;
    std::vector<QString> names_;
};
