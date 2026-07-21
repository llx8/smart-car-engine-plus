#pragma once

#include <QWidget>
#include <QTableWidget>
#include <vector>
#include <cstdint>

class DtcListWidget : public QWidget {
    Q_OBJECT
public:
    explicit DtcListWidget(QWidget* parent = nullptr);

public slots:
    void refresh();

private slots:
    void onDoubleClick(int row, int);

private:
    void queryFreezeFrame(int idx);

    QTableWidget* table_;
    std::vector<uint32_t> freezeData_;
};
