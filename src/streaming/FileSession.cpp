#include "framework4cpp/StreamingSessions.h"

#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

namespace framework4cpp {

FileSession::FileSession(const FileInputSettings &settings, GlobalBuffer &buffer)
    : StreamingSession(buffer), settings_(settings) {}

void FileSession::run() {
    if (!settings_.enabled) {
        // 無効化されている場合は何もせず終了
        return;
    }

    // 監視対象のファイルをバイナリモードで開く
    std::ifstream input(settings_.path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open input file: " + settings_.path);
    }

    // 読み取りバッファを設定されたサイズで確保
    std::vector<char> temp(settings_.readChunkSize);

    while (isRunning()) {
        // ファイルからデータを読み取る
        input.read(temp.data(), static_cast<std::streamsize>(temp.size()));
        std::streamsize count = input.gcount();
        if (count > 0) {
            // 読み取れた場合はバッファアイテムを構築してキューに投入
            BufferItem item;
            item.source = settings_.path;
            item.timestamp = std::chrono::system_clock::now();
            item.payload.assign(temp.begin(), temp.begin() + count);
            buffer_.push(std::move(item));
        }

        if (count == 0) {
            // 末尾に到達した場合の処理
            if (!settings_.follow) {
                // tail 追従しない場合はループを終了
                break;
            }
            if (!input.good()) {
                // EOF 状態をクリアして次回再読込できるようにする
                input.clear();
            }
            // 新しいデータが書き込まれるまで待機
            std::this_thread::sleep_for(settings_.pollInterval);
        }
    }
}

void FileSession::cleanup() {}

} // namespace framework4cpp

