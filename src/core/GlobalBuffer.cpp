#include "framework4cpp/GlobalBuffer.h"

#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace global_buffer {

namespace {

// メモリマップト利用時に必要なオプションを検証し、未指定項目を補完する
Options normalizeOptions(const Options &input) {
    Options result = input;
    // 容量が 0 の場合はデフォルト値を使う
    if (result.capacity == 0) {
        result.capacity = Options{}.capacity;
    }
    // 最大ペイロードサイズが 0 の場合もデフォルト値へフォールバックする
    if (result.maxPayloadSize == 0) {
        result.maxPayloadSize = Options{}.maxPayloadSize;
    }
    // メモリマップト利用時にバックファイルが空ならデフォルト名を設定する
    if (result.memoryMapped && result.backingFile.empty()) {
        result.backingFile = Options{}.backingFile;
    }
    // フィールド名が空の場合はデフォルトのフィールド名を採用する
    if (result.fieldNames.source.empty()) {
        result.fieldNames.source = Options{}.fieldNames.source;
    }
    if (result.fieldNames.timestamp.empty()) {
        result.fieldNames.timestamp = Options{}.fieldNames.timestamp;
    }
    if (result.fieldNames.payload.empty()) {
        result.fieldNames.payload = Options{}.fieldNames.payload;
    }
    return result;
}

} // namespace

GlobalBuffer::GlobalBuffer(const Options &options)
    : options_(normalizeOptions(options)), capacity_(options_.capacity), fieldNames_(options_.fieldNames) {
    // 容量が 0 のままならば利用できないため例外を投げる
    if (capacity_ == 0) {
        throw std::invalid_argument("GlobalBuffer capacity must be greater than zero");
    }
    if (options_.memoryMapped) {
        // メモリマップト有効時は必要なパラメータが揃っているかを再確認する
        if (options_.backingFile.empty()) {
            throw std::invalid_argument("Backing file must be provided when memory mapping is enabled");
        }
        if (options_.maxPayloadSize == 0) {
            throw std::invalid_argument(
                "Max payload size must be greater than zero when memory mapping is enabled");
        }
        slotSize_ = sizeof(std::uint32_t) + options_.maxPayloadSize;
        mappedSize_ = slotSize_ * capacity_;
        initializeMapping();
    }
}

GlobalBuffer::GlobalBuffer(std::size_t capacity, bool memoryMapped, const std::string &backingFile,
                           std::size_t maxPayloadSize)
    : GlobalBuffer(Options{capacity, memoryMapped, backingFile, maxPayloadSize}) {}

GlobalBuffer::~GlobalBuffer() {
    // 破棄時に待機スレッドを解除する
    shutdown();
    releaseMapping();
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
    // BufferItem に対してこのバッファで利用するフィールド名を適用する
    item.fieldNames = fieldNames_;

    std::size_t slotIndex = QueueEntry::kInvalidSlot;
    std::size_t payloadSize = item.payload.size();
    if (options_.memoryMapped) {
        // メモリマップトバッファが初期化済みかを確認する
        if (!mappedView_) {
            throw std::runtime_error("Memory-mapped buffer is not initialized");
        }
        // 許容サイズを超えるデータは登録できないため例外を送出する
        if (payloadSize > options_.maxPayloadSize) {
            throw std::runtime_error("Payload size exceeds configured maximum for memory-mapped buffer");
        }
        // 現在のスロット位置にヘッダ（サイズ情報）とデータ本体を書き込む
        slotIndex = writeIndex_;
        std::size_t offset = slotIndex * slotSize_;
        std::uint32_t storedSize = static_cast<std::uint32_t>(payloadSize);
        std::memcpy(mappedView_ + offset, &storedSize, sizeof(storedSize));
        if (payloadSize > 0) {
            std::memcpy(mappedView_ + offset + sizeof(storedSize), item.payload.data(), payloadSize);
        }
        // 次回書き込み用にリングインデックスを更新し、バッファ内のペイロードは破棄する
        writeIndex_ = (writeIndex_ + 1) % capacity_;
        item.payload.clear();
    }
    // 受け取ったアイテムを待ち行列に追加する
    queue_.push_back(QueueEntry{std::move(item), payloadSize, slotIndex});
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
    QueueEntry entry = std::move(queue_.front());
    queue_.pop_front();
    // 空きができたことを push 側に知らせる
    canPush_.notify_one();
    return materializeEntry(std::move(entry));
}

std::optional<BufferItem> GlobalBuffer::tryPop() {
    // ノンブロッキング取得のため即座にロックを取る
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    // データがあれば先頭を返す
    QueueEntry entry = std::move(queue_.front());
    queue_.pop_front();
    canPush_.notify_one();
    return materializeEntry(std::move(entry));
}

void GlobalBuffer::shutdown() {
    // 終了フラグを立て待機スレッドを起こす
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = true;
    canPush_.notify_all();
    canPop_.notify_all();
}

BufferItem GlobalBuffer::materializeEntry(QueueEntry entry) const {
    BufferItem item = std::move(entry.item);
    if (options_.memoryMapped && entry.slotIndex != QueueEntry::kInvalidSlot) {
        // メモリマップトファイルから指定スロットのデータを読み戻す
        std::size_t offset = entry.slotIndex * slotSize_;
        std::uint32_t storedSize = 0;
        std::memcpy(&storedSize, mappedView_ + offset, sizeof(storedSize));
        if (storedSize != entry.payloadSize) {
            throw std::runtime_error("Memory-mapped payload size mismatch detected");
        }
        item.payload.resize(entry.payloadSize);
        if (entry.payloadSize > 0) {
            std::memcpy(item.payload.data(), mappedView_ + offset + sizeof(storedSize), entry.payloadSize);
        }
    }
    return item;
}

void GlobalBuffer::initializeMapping() {
#ifdef _WIN32
    // バックファイルを開き、指定サイズに拡張してからマップする
    openFileHandle();
    ensureFileSize(mappedSize_);
    mapView(mappedSize_);
#else
    // POSIX システムでは open/ftruncate/mmap で共有領域を確保する
    fileDescriptor_ = ::open(options_.backingFile.c_str(), O_RDWR | O_CREAT, 0666);
    if (fileDescriptor_ == -1) {
        throw std::runtime_error("Failed to open backing file for memory-mapped buffer");
    }
    if (::ftruncate(fileDescriptor_, static_cast<off_t>(mappedSize_)) == -1) {
        ::close(fileDescriptor_);
        fileDescriptor_ = -1;
        throw std::runtime_error("Failed to resize backing file for memory-mapped buffer");
    }
    void *view = ::mmap(nullptr, mappedSize_, PROT_READ | PROT_WRITE, MAP_SHARED, fileDescriptor_, 0);
    if (view == MAP_FAILED) {
        ::close(fileDescriptor_);
        fileDescriptor_ = -1;
        throw std::runtime_error("Failed to map backing file into memory");
    }
    mappedView_ = static_cast<std::uint8_t *>(view);
#endif
}

void GlobalBuffer::releaseMapping() {
    if (!options_.memoryMapped) {
        return;
    }
#ifdef _WIN32
    // Windows ではビュー→マッピング→ファイルの順でクローズする
    unmapView();
    closeMappingHandle();
    closeFileHandle();
#else
    // POSIX ではマップ解除とファイルディスクリプタのクローズを行う
    if (mappedView_ && mappedView_ != MAP_FAILED) {
        ::munmap(mappedView_, mappedSize_);
    }
    if (fileDescriptor_ != -1) {
        ::close(fileDescriptor_);
    }
#endif
    mappedView_ = nullptr;
    mappedSize_ = 0;
    writeIndex_ = 0;
}

#ifdef _WIN32
void GlobalBuffer::openFileHandle() {
    // Windows API でバックファイルを開く（存在しなければ作成）
    HANDLE handle = CreateFileA(options_.backingFile.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open backing file for memory-mapped buffer");
    }
    fileHandle_ = handle;
}

void GlobalBuffer::ensureFileSize(std::uint64_t size) {
    // ファイルポインタを移動して指定サイズに切り上げる
    LARGE_INTEGER li{};
    li.QuadPart = size;
    if (SetFilePointerEx(static_cast<HANDLE>(fileHandle_), li, nullptr, FILE_BEGIN) == 0 ||
        SetEndOfFile(static_cast<HANDLE>(fileHandle_)) == 0) {
        CloseHandle(static_cast<HANDLE>(fileHandle_));
        fileHandle_ = reinterpret_cast<void *>(-1);
        throw std::runtime_error("Failed to resize backing file for memory-mapped buffer");
    }
}

void GlobalBuffer::mapView(std::uint64_t size) {
    // 指定サイズでファイルマッピングを作成し、全域をマップする
    HANDLE mapping = CreateFileMappingA(static_cast<HANDLE>(fileHandle_), nullptr, PAGE_READWRITE,
                                        static_cast<DWORD>(size >> 32), static_cast<DWORD>(size & 0xFFFFFFFF), nullptr);
    if (!mapping) {
        CloseHandle(static_cast<HANDLE>(fileHandle_));
        fileHandle_ = reinterpret_cast<void *>(-1);
        throw std::runtime_error("Failed to create file mapping for memory-mapped buffer");
    }
    mappingHandle_ = mapping;
    void *view = MapViewOfFile(mappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!view) {
        CloseHandle(mappingHandle_);
        mappingHandle_ = nullptr;
        CloseHandle(static_cast<HANDLE>(fileHandle_));
        fileHandle_ = reinterpret_cast<void *>(-1);
        throw std::runtime_error("Failed to map backing file into memory");
    }
    mappedView_ = static_cast<std::uint8_t *>(view);
}

void GlobalBuffer::unmapView() {
    // マップ済みビューを解放する
    if (mappedView_) {
        UnmapViewOfFile(mappedView_);
        mappedView_ = nullptr;
    }
}

void GlobalBuffer::closeMappingHandle() {
    // ファイルマッピングハンドルをクローズする
    if (mappingHandle_) {
        CloseHandle(static_cast<HANDLE>(mappingHandle_));
        mappingHandle_ = nullptr;
    }
}

void GlobalBuffer::closeFileHandle() {
    // 元のファイルハンドルをクローズする
    if (fileHandle_ != reinterpret_cast<void *>(-1)) {
        CloseHandle(static_cast<HANDLE>(fileHandle_));
        fileHandle_ = reinterpret_cast<void *>(-1);
    }
}
#endif

} // namespace global_buffer
