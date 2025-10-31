#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace global_buffer {

// グローバルバッファでデータを扱う際のフィールド名セット
struct FieldNames {
    // 発生元を表すフィールド名（デフォルトは "source"）
    std::string source{"source"};
    // タイムスタンプを表すフィールド名（デフォルトは "timestamp"）
    std::string timestamp{"timestamp"};
    // ペイロードを表すフィールド名（デフォルトは "payload"）
    std::string payload{"payload"};
};

// 入出力で共有する 1 レコード分のデータを格納する構造体
struct BufferItem {
    // データの発生元（ファイルパスやポートなど）
    std::string source;
    // データを受信した時刻
    std::chrono::system_clock::time_point timestamp;
    // 受信した生データのバイト列
    std::vector<std::uint8_t> payload;
    // 取り扱いフィールド名（用途に応じてデフォルトから上書き可能）
    FieldNames fieldNames;
};

// グローバルバッファの利用時に指定可能なオプション一式
struct Options {
    // バッファに保持できる最大アイテム数（未指定ならデフォルト値を利用）
    std::size_t capacity{1024};
    // メモリマップトファイルを利用するかどうか
    bool memoryMapped{false};
    // メモリマップトファイルの保存先（未設定時はデフォルトファイル名を使用）
    std::string backingFile{"global_buffer.mmap"};
    // 各アイテムの最大ペイロードサイズ（メモリマップト利用時に必須）
    std::size_t maxPayloadSize{4096};
    // BufferItem 内のフィールド名セット（指定が無ければデフォルト値）
    FieldNames fieldNames{};
};

// スレッド間で共有するリングバッファの実装
class GlobalBuffer {
public:
    // オプション構造体を受け取って初期化する
    explicit GlobalBuffer(const Options &options = Options{});
    // 旧インターフェース用の補助コンストラクタ（後方互換性保持）
    GlobalBuffer(std::size_t capacity, bool memoryMapped = false, const std::string &backingFile = {},
                 std::size_t maxPayloadSize = 0);
    // バッファ破棄時に保留中の待機スレッドを開放する
    ~GlobalBuffer();

    // 新しいデータをバッファに追加する（必要に応じて待機）
    void push(BufferItem item);
    // データを 1 件取り出す（データが来るまで待機）
    std::optional<BufferItem> pop();
    // ノンブロッキングでデータを 1 件取り出す
    std::optional<BufferItem> tryPop();

    // バッファの終了フラグを立て、待機スレッドを解除する
    void shutdown();

private:
    // 利用時のオプションを保持
    Options options_{};
    // 実際に格納できるアイテム数
    std::size_t capacity_{};
    // BufferItem に反映するフィールド名のセット
    FieldNames fieldNames_{};

    // 排他制御用ミューテックス
    std::mutex mutex_;
    // push が可能になるまで待たせるための条件変数
    std::condition_variable canPush_;
    // pop が可能になるまで待たせるための条件変数
    std::condition_variable canPop_;
    struct QueueEntry {
        // メモリマップトファイル未使用時を表すスロット番号
        static constexpr std::size_t kInvalidSlot = std::numeric_limits<std::size_t>::max();
        // アイテム本体（ソースやタイムスタンプなど）
        BufferItem item;
        // 保持しているペイロードのサイズ
        std::size_t payloadSize{0};
        // メモリマップトファイル上のスロット番号
        std::size_t slotIndex{kInvalidSlot};
    };

    // 実データを保持する待ち行列
    std::deque<QueueEntry> queue_;
    // 終了状態を示すフラグ
    bool shutdown_{false};

    // メモリマップトファイルの 1 スロットあたりの確保バイト数
    std::size_t slotSize_{0};
    // 次に書き込むスロットのインデックス
    std::size_t writeIndex_{0};

    // メモリマップトファイルを初期化する
    void initializeMapping();
    // メモリマップトファイルを解放する
    void releaseMapping();
    // メモリマップトファイルからペイロードを復元する
    BufferItem materializeEntry(QueueEntry entry) const;

#ifdef _WIN32
    void ensureFileSize(std::uint64_t size);
    void openFileHandle();
    void mapView(std::uint64_t size);
    void closeFileHandle();
    void closeMappingHandle();
    void unmapView();
    void *mappingHandle_{nullptr};
    void *fileHandle_{reinterpret_cast<void *>(-1)};
#else
    int fileDescriptor_{-1};
#endif
    std::uint8_t *mappedView_{nullptr};
    std::size_t mappedSize_{0};
};

} // namespace global_buffer

// フレームワーク内部からの呼び出し互換性を維持するための別名定義
namespace framework4cpp {
using BufferItem = ::global_buffer::BufferItem;
using GlobalBuffer = ::global_buffer::GlobalBuffer;
using GlobalBufferOptions = ::global_buffer::Options;
} // namespace framework4cpp

