#include "framework4cpp/StreamingSessions.h"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace framework4cpp {

#ifndef _WIN32
namespace {

// ボーレート設定値を termios が要求する定数へ変換する
speed_t toTermiosBaud(unsigned int baudRate) {
    switch (baudRate) {
    case 0:
        return B0;
    case 50:
        return B50;
    case 75:
        return B75;
    case 110:
        return B110;
    case 134:
        return B134;
    case 150:
        return B150;
    case 200:
        return B200;
    case 300:
        return B300;
    case 600:
        return B600;
    case 1200:
        return B1200;
    case 1800:
        return B1800;
    case 2400:
        return B2400;
    case 4800:
        return B4800;
    case 9600:
        return B9600;
#ifdef B14400
    case 14400:
        return B14400;
#endif
    case 19200:
        return B19200;
#ifdef B28800
    case 28800:
        return B28800;
#endif
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
#ifdef B128000
    case 128000:
        return B128000;
#endif
#ifdef B230400
    case 230400:
        return B230400;
#endif
#ifdef B460800
    case 460800:
        return B460800;
#endif
#ifdef B500000
    case 500000:
        return B500000;
#endif
#ifdef B576000
    case 576000:
        return B576000;
#endif
#ifdef B921600
    case 921600:
        return B921600;
#endif
#ifdef B1000000
    case 1000000:
        return B1000000;
#endif
#ifdef B1152000
    case 1152000:
        return B1152000;
#endif
#ifdef B1500000
    case 1500000:
        return B1500000;
#endif
#ifdef B2000000
    case 2000000:
        return B2000000;
#endif
#ifdef B2500000
    case 2500000:
        return B2500000;
#endif
#ifdef B3000000
    case 3000000:
        return B3000000;
#endif
#ifdef B3500000
    case 3500000:
        return B3500000;
#endif
#ifdef B4000000
    case 4000000:
        return B4000000;
#endif
    default:
        // 未対応の速度の場合は一般的な 9600bps を返す
        return B9600;
    }
}

} // namespace
#endif

SerialSession::SerialSession(const SerialInputSettings &settings, GlobalBuffer &buffer)
    : StreamingSession(buffer), settings_(settings) {}

void SerialSession::run() {
    if (!settings_.enabled) {
        // 無効化されている場合は即座に終了
        return;
    }

#ifdef _WIN32
    // Win32 API を利用してシリアルポートを開く
    HANDLE handle = CreateFileA(settings_.port.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open serial port: " + settings_.port);
    }
    handle_ = reinterpret_cast<std::intptr_t>(handle);

    // タイムアウト設定を構成する
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(handle, &timeouts);

    // DCB 構造体で通信条件を指定する
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle, &dcb)) {
        CloseHandle(handle);
        handle_ = -1;
        throw std::runtime_error("Failed to query serial port state");
    }
    dcb.BaudRate = settings_.baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(handle, &dcb)) {
        CloseHandle(handle);
        handle_ = -1;
        throw std::runtime_error("Failed to configure serial port");
    }

    // 読み取り用の一時バッファを確保する
    std::vector<std::uint8_t> buffer(settings_.readChunkSize);
    DWORD bytesRead = 0;
    while (isRunning()) {
        // 非同期的にデータを読み取る
        if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            break;
        }
        if (bytesRead > 0) {
            // 読み取った内容をバッファアイテムに詰めて送出する
            BufferItem item;
            item.source = settings_.port;
            item.timestamp = std::chrono::system_clock::now();
            item.payload.assign(buffer.begin(), buffer.begin() + bytesRead);
            buffer_.push(std::move(item));
        } else {
            // データが無かった場合は少し待機してから再度試行
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
#else
    // POSIX API を用いてシリアルポートを開く
    handle_ = ::open(settings_.port.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (handle_ < 0) {
        throw std::runtime_error("Failed to open serial port: " + settings_.port);
    }

    // 現在のポート設定を取得
    termios tty{};
    if (tcgetattr(static_cast<int>(handle_), &tty) != 0) {
        throw std::runtime_error("Failed to get serial attributes");
    }

    // RAW モード設定と速度指定を行う
    cfmakeraw(&tty);
    const auto baud = toTermiosBaud(settings_.baudRate);
    if (cfsetispeed(&tty, baud) != 0 || cfsetospeed(&tty, baud) != 0) {
        throw std::runtime_error("Failed to set serial baud rate");
    }
    tty.c_cflag |= (CLOCAL | CREAD);
    if (tcsetattr(static_cast<int>(handle_), TCSANOW, &tty) != 0) {
        throw std::runtime_error("Failed to set serial attributes");
    }

    // 読み取り用の一時バッファを確保
    std::vector<std::uint8_t> buffer(settings_.readChunkSize);
    while (isRunning()) {
        // 非ブロッキングでデータを読み取る
        ssize_t count = ::read(static_cast<int>(handle_), buffer.data(), buffer.size());
        if (count > 0) {
            // 受信データをバッファアイテムに詰めて共有バッファへ投入
            BufferItem item;
            item.source = settings_.port;
            item.timestamp = std::chrono::system_clock::now();
            item.payload.assign(buffer.begin(), buffer.begin() + count);
            buffer_.push(std::move(item));
        } else {
            if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // データ未到着の場合は少し待って再試行
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            // その他のエラーや切断時はループを抜ける
            break;
        }
    }
#endif
}

void SerialSession::cleanup() {
#ifdef _WIN32
    // 開いているハンドルがあればクローズする
    if (handle_ != -1) {
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
        handle_ = -1;
    }
#else
    // POSIX ハンドルをクローズする
    if (handle_ >= 0) {
        ::close(static_cast<int>(handle_));
        handle_ = -1;
    }
#endif
}

} // namespace framework4cpp

