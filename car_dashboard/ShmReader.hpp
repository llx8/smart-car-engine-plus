#pragma once

#include <QObject>
#include <QSocketNotifier>

struct ShmHeader;
struct ShmBlock;

class ShmReader : public QObject {
    Q_OBJECT
public:
    // shmid: System V 共享内存 ID
    // eventfd_fd: car_core 创建的 eventfd 文件描述符（通过命令行参数传入）
    explicit ShmReader(int shmid, int eventfd_fd, QObject* parent = nullptr);
    ~ShmReader() override;

    // 返回本地副本的只读指针。调用者不应持有该指针——每次 dataUpdated 后重新获取
    const ShmBlock* data() const { return local_copy_; }

    const ShmHeader* header() const { return header_; }

    uint32_t readIndex() const;

signals:
    void dataUpdated();

private slots:
    void onDataReady(int fd);

private:
    int eventfd_fd_;
    QSocketNotifier* notifier_ = nullptr;

    // 共享内存相关
    void* raw_shm_      = nullptr;   // shmat 原始指针
    ShmHeader* header_   = nullptr;   // 对齐后的 header 地址
    ShmBlock* shm_buf_[2] = {nullptr, nullptr};  // Ping-Pong 双缓冲

    // 本地副本 —— 在堆上分配，读端写入
    ShmBlock* local_copy_ = nullptr;

    static constexpr int kMaxRetry = 5;
};