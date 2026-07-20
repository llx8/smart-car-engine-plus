#include "ShmReader.hpp"
#include <ShmLayout.hpp>
#include <sys/shm.h>
#include <unistd.h>
#include <iostream>

uint32_t ShmReader::readIndex() const {
    if (!header_) return 0;
    return header_->read_index.load(std::memory_order_acquire);
}

ShmReader::ShmReader(int shmid, int eventfd_fd, QObject* parent)
    : QObject(parent), eventfd_fd_(eventfd_fd)
{
    // ── 步骤 1：映射共享内存 ──
    raw_shm_ = shmat(shmid, nullptr, 0);
    if (raw_shm_ == reinterpret_cast<void*>(-1)) {
        std::cerr << "[ShmReader] shmat failed" << std::endl;
        return;
    }

    // ── 步骤 2：64 字节对齐，按布局取出 header 和双缓冲 ──
    uintptr_t aligned = (reinterpret_cast<uintptr_t>(raw_shm_) + 63) & ~static_cast<uintptr_t>(63);
    header_     = reinterpret_cast<ShmHeader*>(aligned);
    shm_buf_[0] = reinterpret_cast<ShmBlock*>(aligned + sizeof(ShmHeader));
    shm_buf_[1] = reinterpret_cast<ShmBlock*>(
        aligned + sizeof(ShmHeader) + sizeof(ShmBlock));

    // ── 步骤 3：验证 magic（只读，不需要原子操作） ──
    if (header_->magic != 0xCAFE0001) {
        std::cerr << "[ShmReader] bad magic: shared memory not initialized by car_core"
                  << std::endl;
        return;
    }

    // ── 步骤 4：分配本地副本 ──
    local_copy_ = new ShmBlock;

    // ── 步骤 5：QSocketNotifier 监听 eventfd ──
    // Qt4 风格 connect（兼容性更好，不用函数指针语法）
    notifier_ = new QSocketNotifier(eventfd_fd_, QSocketNotifier::Read, this);
    connect(notifier_, SIGNAL(activated(int)), this, SLOT(onDataReady(int)));
}

ShmReader::~ShmReader()
{
    if (local_copy_) {
        delete local_copy_;
        local_copy_ = nullptr;
    }
    // notifier_ 由 Qt 父对象机制自动销毁，不需要手动 delete
    // 但如果设置了 parent=nullptr 则需手动清理；此处 parent=this，安全
    if (raw_shm_ && raw_shm_ != reinterpret_cast<void*>(-1)) {
        shmdt(raw_shm_);
    }
    // eventfd 关闭：注意——谁创建谁负责关闭。
    // 此处的 eventfd 由 car_core 创建并通过命令行传过来，
    // car_dashboard 作为使用者，关闭它合情合理，避免 fd 泄漏。
    if (eventfd_fd_ >= 0) {
        close(eventfd_fd_);
    }
}

void ShmReader::onDataReady(int /*fd*/)
{
    // ── 步骤 1：消费 eventfd 事件 ⚠️ 必须第一步，否则 QSocketNotifier 死循环 ──
    uint64_t cnt = 0;
    if (read(eventfd_fd_, &cnt, sizeof(cnt)) != sizeof(cnt)) {
        return;
    }

    // ── 步骤 2：Ping-Pong 读端协议 ──
    bool success = false;
    for (int retry = 0; retry < kMaxRetry; ++retry) {
        // 读活跃索引
        uint32_t idx = header_->read_index.load(std::memory_order_acquire);
        // 拷贝活跃缓冲区到本地
        std::memcpy(local_copy_, shm_buf_[idx], sizeof(ShmBlock));
        // recheck：索引未变 → 写端未在拷贝期间切换 → 数据一致
        uint32_t idx2 = header_->read_index.load(std::memory_order_acquire);
        if (idx == idx2) {
            success = true;
            break;
        }
        // idx 变了 → 写端切换到了新 buf，重试
    }

    if (!success) {
        std::cerr << "[ShmReader] Ping-Pong read failed after " << kMaxRetry
                  << " retries, using last copy" << std::endl;
    }

    // ── 步骤 3：通知 Qt 渲染 ──
    emit dataUpdated();
}
