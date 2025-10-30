#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace framework4cpp {

// 入出力で共有する 1 レコード分のデータを格納する構造体
struct BufferItem {
    // データの発生元（ファイルパスやポートなど）
    std::string source;
    // データを受信した時刻
    std::chrono::system_clock::time_point timestamp;
    // 受信した生データのバイト列
    std::vector<std::uint8_t> payload;
};

// スレッド間で共有するリングバッファの実装
class GlobalBuffer {
public:
    // バッファ容量とメモリマップトファイルの有無を指定して初期化する
    explicit GlobalBuffer(std::size_t capacity, bool memoryMapped = false, const std::string &backingFile = {});
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
    // 格納可能なアイテム数
    std::size_t capacity_;
    // メモリマップトファイル利用フラグ（現状はプレースホルダー）
    bool memoryMapped_;
    // メモリマップトファイルのパス
    std::string backingFile_;

    // 排他制御用ミューテックス
    std::mutex mutex_;
    // push が可能になるまで待たせるための条件変数
    std::condition_variable canPush_;
    // pop が可能になるまで待たせるための条件変数
    std::condition_variable canPop_;
    // 実データを保持する待ち行列
    std::deque<BufferItem> queue_;
    // 終了状態を示すフラグ
    bool shutdown_{false};
};

} // namespace framework4cpp

