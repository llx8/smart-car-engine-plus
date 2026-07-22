#include "DtcListWidget.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QHeaderView>
#include <QMessageBox>
#include <QColor>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include "CarMsg.hpp"
#include <DtcEngine.hpp>

DtcListWidget::DtcListWidget(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel("DTC 故障列表", this);
    title->setStyleSheet("color: white; font-size: 14px; font-weight: bold;");
    header->addWidget(title);
    header->addStretch();
    auto* refreshBtn = new QPushButton("刷新", this);
    refreshBtn->setStyleSheet(
        "QPushButton { background: #2a5a9c; color: white; border: none;"
        "border-radius: 3px; padding: 4px 12px; }"
        "QPushButton:hover { background: #3a7acc; }");
    connect(refreshBtn, &QPushButton::clicked, this, &DtcListWidget::refresh);
    header->addWidget(refreshBtn);
    lay->addLayout(header);

    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({"故障码", "严重度", "状态", "描述"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(28);  // 行高 28px,避免字被横向滚动条盖住
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 最后一列 stretch,不需要横向滚动
    table_->setStyleSheet(
        "QTableWidget { background: #1a1a1a; color: white; gridline-color: #333;"
        "border: 1px solid #444; font-size: 12px; }"
        "QTableWidget::item { padding: 4px; }"
        "QHeaderView::section { background: #222; color: #999;"
        "border: none; padding: 4px; }");
    table_->setColumnWidth(0, 80);
    table_->setColumnWidth(1, 60);
    table_->setColumnWidth(2, 60);
    lay->addWidget(table_);

    connect(table_, &QTableWidget::cellDoubleClicked,
            this, &DtcListWidget::onDoubleClick);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &DtcListWidget::refresh);
    timer->start(2000);
}

void DtcListWidget::refresh() {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "/tmp/car_core.sock", sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd); return;
    }

    auto req = makeReq(ModId::DTC, CmdType::GET_ALL, 0);
    ::send(fd, &req, sizeof(req), MSG_NOSIGNAL);

    CarMsgResp resp{};
    ::recv(fd, &resp, sizeof(resp), 0);
    ::close(fd);

    if (resp.result != 0) return;

    int count = resp.value[0];
    table_->setRowCount(count);
    freezeData_.resize(count);

    for (int i = 0; i < count && i < 3; ++i) {
        uint32_t code = (static_cast<uint32_t>(resp.value[1 + i*5 + 0]) << 24)
                      | (static_cast<uint32_t>(resp.value[1 + i*5 + 1]) << 16)
                      | (static_cast<uint32_t>(resp.value[1 + i*5 + 2]) << 8)
                      |  static_cast<uint32_t>(resp.value[1 + i*5 + 3]);
        uint8_t sev = resp.value[1 + i*5 + 4];

        char buf[8];
        char cat = static_cast<char>((code >> 24) & 0xFF);
        snprintf(buf, 8, "%c%04X", cat, code & 0xFFFF);
        table_->setItem(i, 0, new QTableWidgetItem(buf));
        table_->setItem(i, 1, new QTableWidgetItem(
            sev == 1 ? "info" : (sev == 2 ? "warn" : "crit")));
        table_->setItem(i, 2, new QTableWidgetItem("活跃"));

        const char* desc = "未知";
        switch (code) {
        case dtc_codes::P0115: desc = "发动机水温过高"; break;
        case dtc_codes::C0035: desc = "ABS 轮速异常"; break;
        case dtc_codes::B0020: desc = "气囊碰撞触发"; break;
        case dtc_codes::P0560: desc = "系统电压低"; break;
        }
        table_->setItem(i, 3, new QTableWidgetItem(desc));

        QColor rowColor = (sev == 3) ? QColor(80, 20, 20)
                       : (sev == 2) ? QColor(80, 60, 10) : QColor(30, 40, 30);
        for (int col = 0; col < 4; ++col) {
            if (auto* item = table_->item(i, col))
                item->setBackground(rowColor);
        }

        freezeData_[i] = code;
    }
}

void DtcListWidget::onDoubleClick(int row, int) {
    if (row < 0 || row >= static_cast<int>(freezeData_.size())) return;
    queryFreezeFrame(row);
}

void DtcListWidget::queryFreezeFrame(int idx) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "/tmp/car_core.sock", sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd); return;
    }

    auto req = makeReq(ModId::DTC, CmdType::READ, 1, static_cast<uint8_t>(idx));
    ::send(fd, &req, sizeof(req), MSG_NOSIGNAL);
    CarMsgResp resp{};
    ::recv(fd, &resp, sizeof(resp), 0);
    ::close(fd);

    float speed, water, battery;
    int32_t rpm;
    std::memcpy(&speed,   resp.value,      4);
    std::memcpy(&rpm,     resp.value + 4,  4);
    std::memcpy(&water,   resp.value + 8,  4);
    std::memcpy(&battery, resp.value + 12, 4);

    QString info = QString(
        "冻结帧 — %1\n\n"
        "车速:    %2 km/h\n"
        "转速:    %3 rpm\n"
        "水温:    %4 °C\n"
        "电瓶:    %5 V")
        .arg(table_->item(idx, 0)->text())
        .arg(speed, 0, 'f', 1)
        .arg(rpm)
        .arg(water, 0, 'f', 1)
        .arg(battery, 0, 'f', 1);

    QMessageBox::information(this, "冻结帧", info);
}
