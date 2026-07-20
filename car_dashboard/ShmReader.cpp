#include "ShmReader.hpp"
#include <ShmLayout.hpp>
#include <sys/shm.h>
#include <unistd.h>
#include <iostream>

ShmReader::ShmReader(int shmid, int eventfd_fd, QObject* parent)
    : QObject(parent), eventfd_fd_(eventfd_fd)
{
    // ── 步骤 1：映射共享内存 ──
    raw_shm_ = shmat(shmid, nullptr, 0);
    if (raw_shm_ == reinterpret_cast<void*>(-1)) {
        std::cerr << "[ShmReader] shmat failed" << std::endl;
        return;
    }

    // ── 步骤 2：64 字节对齐，按布局取出 header 和 shm_block ──
    // 与 car_core/main.cpp 写端对齐逻辑完全一致
    uintptr_t aligned = (reinterpret_cast<uintptr_t>(raw_shm_) + 63) & ~static_cast<uintptr_t>(63);
    header_    = reinterpret_cast<ShmHeader*>(aligned);
    shm_block_ = reinterpret_cast<ShmBlock*>(aligned + sizeof(ShmHeader));

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
        return; // eventfd 读失败，异常情况，跳过本轮
    }

    // ── 步骤 2：Seqlock 读端协议 ──
    bool success = false;
    for (int retry = 0; retry < kMaxRetry; ++retry) {
        uint32_t v1 = header_->version.load(std::memory_order_acquire);

        // 写端正持有锁（version 为奇数），自旋等待下一轮
        if (v1 & 1) continue;

        // 拷贝整个数据块到本地副本
        std::memcpy(local_copy_, shm_block_, sizeof(ShmBlock));

        uint32_t v2 = header_->version.load(std::memory_order_acquire);

        // version 未变化 → 拷贝期间没有并发写入 → 数据一致
        if (v1 == v2) {
            success = true;
            break;
        }
        // v1 != v2 → 写端在 memcpy 期间完成了写入，数据可能撕裂，重试
    }

    if (!success) {
        // 降级：重试全部失败，仍使用最后一次拷贝的数据，但打印告警
        // 20Hz CAN 刷新下，下次 eventfd 触发即可读到一致版本
        std::cerr << "[ShmReader] Seqlock read failed after " << kMaxRetry
                  << " retries, using last copy" << std::endl;
    }

    // ── 步骤 3：通知 Qt 渲染 ──
    emit dataUpdated();
}
