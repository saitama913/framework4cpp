#include "framework4cpp/GlobalBuffer.h"

#include <stdexcept>

namespace framework4cpp {

GlobalBuffer::GlobalBuffer(std::size_t capacity, bool memoryMapped, const std::string &backingFile)
    : capacity_(capacity), memoryMapped_(memoryMapped), backingFile_(backingFile) {
    // 容量が 0 の場合は運用できないため例外を送出する
    if (capacity_ == 0) {
        throw std::invalid_argument("GlobalBuffer capacity must be greater than zero");
    }
    // メモリマップト関連は現段階では未実装なので未使用警告を抑制
    (void)memoryMapped_;
    (void)backingFile_;
}

GlobalBuffer::~GlobalBuffer() {
    // 破棄時に待機スレッドを解除する
    shutdown();
}

void GlobalBuffer::push(BufferItem item) {
    // 排他制御のためにロックを獲得する
    std::unique_lock<std::mutex> lock(mutex_);
    // バッファに空きができるか、終了指示が出るまで待機する
    canPush_.wait(lock, [this]() { return shutdown_ || queue_.size() < capacity_; });
    if (shutdown_) {
        // 終了中は新しいデータを受け付けない
        return;
    }
    // 受け取ったアイテムを待ち行列に追加する
    queue_.emplace_back(std::move(item));
    // データが追加されたことを待機中のポップ側へ通知する
    canPop_.notify_one();
}

std::optional<BufferItem> GlobalBuffer::pop() {
    // 排他制御のためにロックを獲得する
    std::unique_lock<std::mutex> lock(mutex_);
    // データが入るか終了するまで待機する
    canPop_.wait(lock, [this]() { return shutdown_ || !queue_.empty(); });
    if (queue_.empty()) {
        // 終了処理などで空の場合は nullopt を返す
        return std::nullopt;
    }
    // 先頭のアイテムを取り出して返す
    BufferItem item = std::move(queue_.front());
    queue_.pop_front();
    // 空きができたことを push 側に知らせる
    canPush_.notify_one();
    return item;
}

std::optional<BufferItem> GlobalBuffer::tryPop() {
    // ノンブロッキング取得のため即座にロックを取る
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    // データがあれば先頭を返す
    BufferItem item = std::move(queue_.front());
    queue_.pop_front();
    canPush_.notify_one();
    return item;
}

void GlobalBuffer::shutdown() {
    // 終了フラグを立て待機スレッドを起こす
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = true;
    canPush_.notify_all();
    canPop_.notify_all();
}

} // namespace framework4cpp

