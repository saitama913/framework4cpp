#include "framework4cpp/Config.h"
#include "framework4cpp/CsvWriter.h"
#include "framework4cpp/GlobalBuffer.h"
#include "framework4cpp/StreamingSessions.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

// メインループ終了を外部から指示するためのフラグ
std::atomic<bool> g_shouldExit{false};

// SIGINT/SIGTERM を受けた際に終了フラグを立てる
void signalHandler(int) {
    g_shouldExit = true;
}

} // namespace

int main(int argc, char **argv) {
    // コマンドライン引数から設定ファイルのパスを取得（未指定時は config.ini）
    std::string configPath = "config.ini";
    if (argc > 1) {
        configPath = argv[1];
    }

    try {
        // 設定ファイルを読み込んで実行時パラメータを取得
        auto config = framework4cpp::Config::loadFromFile(configPath);

        // 共有バッファ用のオプションを準備し、未指定項目はデフォルトを利用
        global_buffer::Options bufferOptions{};
        if (config.buffer.capacity > 0) {
            // 容量が指定されていればデフォルトを上書き
            bufferOptions.capacity = config.buffer.capacity;
        }
        if (config.buffer.maxPayloadSize > 0) {
            // ペイロードの最大サイズ指定があれば反映
            bufferOptions.maxPayloadSize = config.buffer.maxPayloadSize;
        }
        // メモリマップト利用可否を設定（false の場合は backingFile 指定も無視される）
        bufferOptions.memoryMapped = config.buffer.memoryMapped;
        if (!config.buffer.backingFile.empty()) {
            // バックファイル名が明示指定されていれば採用
            bufferOptions.backingFile = config.buffer.backingFile;
        }
        // フィールド名の指定があればバッファオプションに転記
        bufferOptions.fieldNames.source = config.buffer.fieldNames.source;
        bufferOptions.fieldNames.timestamp = config.buffer.fieldNames.timestamp;
        bufferOptions.fieldNames.payload = config.buffer.fieldNames.payload;

        // 共有バッファを設定に従って初期化
        global_buffer::GlobalBuffer buffer(bufferOptions);

        // 有効なセッションのみ生成するためのコンテナ
        std::vector<framework4cpp::StreamingSessionPtr> sessions;
        sessions.reserve(3);
        if (config.fileInput.enabled) {
            // ファイル入力セッションを生成
            sessions.emplace_back(std::make_unique<framework4cpp::FileSession>(config.fileInput, buffer));
        }
        if (config.serialInput.enabled) {
            // シリアル入力セッションを生成
            sessions.emplace_back(std::make_unique<framework4cpp::SerialSession>(config.serialInput, buffer));
        }
        if (config.ipInput.enabled) {
            // ネットワーク入力セッションを生成
            sessions.emplace_back(std::make_unique<framework4cpp::IpSession>(config.ipInput, buffer));
        }

        // CSV への書き込みワーカーを初期化・起動
        framework4cpp::CsvWriter writer(config.csv, buffer);
        writer.start();

        // すべてのセッションを起動
        for (auto &session : sessions) {
            session->start();
        }

        // Ctrl+C などで安全に停止できるようシグナルハンドラを設定
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        std::cout << "Streaming started. Press Enter or send SIGINT/SIGTERM to stop." << std::endl;

        // 終了フラグが立つまで待機（Enter 入力でも終了）
        while (!g_shouldExit.load()) {
            if (std::cin.rdbuf()->in_avail() > 0) {
                std::string line;
                std::getline(std::cin, line);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // セッションを停止してリソースを解放
        for (auto &session : sessions) {
            session->stop();
        }

        // バッファとライターも停止処理を実施
        buffer.shutdown();
        writer.stop();

        return 0;
    } catch (const std::exception &ex) {
        // 初期化や実行中に致命的なエラーが発生した場合はログ出力して終了
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
}

