#include "framework4cpp/CsvWriter.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace framework4cpp {

CsvWriter::CsvWriter(const CsvSettings &settings, GlobalBuffer &buffer)
    : settings_(settings), buffer_(buffer) {}

CsvWriter::~CsvWriter() {
    // オブジェクト破棄時に動作中であれば停止する
    stop();
}

void CsvWriter::start() {
    // まだ開始されていない場合のみ running_ を true にする
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    // 出力ファイルを追記モードで開く
    output_.open(settings_.outputPath, std::ios::out | std::ios::app);
    if (!output_.is_open()) {
        running_.store(false);
        throw std::runtime_error("Failed to open CSV output: " + settings_.outputPath);
    }

    // バックグラウンドで書き込み処理を行うスレッドを起動
    worker_ = std::thread(&CsvWriter::run, this);
}

void CsvWriter::stop() {
    // ループに終了を伝える
    running_.store(false);
    // バッファ側にもシャットダウンを伝播する
    buffer_.shutdown();
    // スレッドがまだ動いていれば join する
    if (worker_.joinable()) {
        worker_.join();
    }
    if (output_.is_open()) {
        // ファイル出力を保護しつつフラッシュ・クローズする
        std::lock_guard<std::mutex> lock(fileMutex_);
        output_.flush();
        output_.close();
    }
}

void CsvWriter::run() {
    using clock = std::chrono::steady_clock;
    // 次にフラッシュする時刻を初期化する
    auto nextFlush = clock::now() + settings_.flushInterval;

    while (true) {
        // グローバルバッファから 1 件取り出す（終了時は nullopt）
        auto item = buffer_.pop();
        if (!item.has_value()) {
            if (!running_.load()) {
                // 停止要求が来ておりデータが無ければループ終了
                break;
            }
            continue;
        }

        // 取得したデータを CSV 形式に整形する
        std::string line = formatRecord(*item);
        {
            // ファイル操作の競合を防ぐためにロックする
            std::lock_guard<std::mutex> lock(fileMutex_);
            output_ << line << '\n';
        }

        if (settings_.flushInterval.count() == 0) {
            // フラッシュ間隔 0 の場合は毎回即時フラッシュ
            std::lock_guard<std::mutex> lock(fileMutex_);
            output_.flush();
        } else if (clock::now() >= nextFlush) {
            // 設定された周期でフラッシュを実行
            std::lock_guard<std::mutex> lock(fileMutex_);
            output_.flush();
            nextFlush = clock::now() + settings_.flushInterval;
        }
    }
}

std::string CsvWriter::formatRecord(const BufferItem &item) const {
    std::ostringstream oss;
    bool firstColumn = true;
    // 列を追加するときの共通処理をラムダでまとめる
    auto appendColumn = [&](const std::string &value) {
        if (!firstColumn) {
            // 2 列目以降は区切り文字を挿入
            oss << settings_.delimiter;
        } else {
            firstColumn = false;
        }
        if (settings_.quoteStrings) {
            // 文字列をエスケープして引用符で囲む
            oss << '"' << escape(value) << '"';
        } else {
            oss << value;
        }
    };

    if (settings_.includeTimestamp) {
        // タイムスタンプをローカル時刻に変換して整形
        std::time_t time = std::chrono::system_clock::to_time_t(item.timestamp);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        std::ostringstream timeStream;
        timeStream << std::put_time(&tm, settings_.timestampFormat.c_str());
        appendColumn(timeStream.str());
    }

    // データの発生源を追加
    appendColumn(item.source);

    // ペイロードを 16 進文字列へ変換して追加
    std::ostringstream payload;
    payload << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < item.payload.size(); ++i) {
        payload << std::setw(2) << static_cast<unsigned int>(item.payload[i]);
        if (i + 1 < item.payload.size()) {
            payload << ' ';
        }
    }
    appendColumn(payload.str());

    return oss.str();
}

std::string CsvWriter::escape(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        // 文字をコピーし、必要なら二重引用符をエスケープ
        escaped.push_back(ch);
        if (ch == '"') {
            escaped.push_back('"');
        }
    }
    return escaped;
}

} // namespace framework4cpp

