#pragma once

#include "framework4cpp/Config.h"
#include "framework4cpp/GlobalBuffer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace framework4cpp {

// 入力セッションの共通インターフェースを提供する抽象クラス
class StreamingSession {
public:
    // 共有バッファの参照を受け取って初期化する
    explicit StreamingSession(GlobalBuffer &buffer) : buffer_(buffer) {}
    virtual ~StreamingSession() = default;

    // セッション処理用のスレッドを起動する
    void start();
    // セッション処理を停止して後片付けを行う
    void stop();
    // セッションが動作中かどうかを問い合わせる
    bool isRunning() const { return running_.load(); }

protected:
    // 派生クラスで具体的な受信ループを実装する
    virtual void run() = 0;
    // リソース解放などの後処理を行うフック
    virtual void cleanup() {}

    // データ格納先のグローバルバッファ参照
    GlobalBuffer &buffer_;

private:
    // スレッドから呼ばれる共通エントリポイント
    void threadMain();

    // 実行スレッドオブジェクト
    std::thread worker_;
    // セッションが稼働中かのフラグ
    std::atomic<bool> running_{false};
};

inline void StreamingSession::start() {
    // まだ起動していない場合にのみ running_ を true に切り替える
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        // 既に起動済みの場合は何もせず戻る
        return;
    }
    // 受信処理用のスレッドを生成する
    worker_ = std::thread(&StreamingSession::threadMain, this);
}

inline void StreamingSession::stop() {
    // ループに終了を指示する
    running_.store(false);
    // スレッドがまだ動作していれば join する
    if (worker_.joinable()) {
        worker_.join();
    }
    // 派生クラス固有の後片付けを呼び出す
    cleanup();
}

inline void StreamingSession::threadMain() {
    try {
        // 派生クラスの run() を実行し、例外は外へ伝播させる
        run();
    } catch (...) {
        // 異常終了時でもフラグを false に戻しておく
        running_.store(false);
        throw;
    }
    // 正常終了時も running_ を false に戻す
    running_.store(false);
}

// ファイルからデータを読み取るセッション
class FileSession : public StreamingSession {
public:
    // ファイル入力設定と共有バッファを受け取って初期化
    FileSession(const FileInputSettings &settings, GlobalBuffer &buffer);

protected:
    // ファイル監視ループを実装
    void run() override;
    // ファイルセッションは特別な後処理を持たない
    void cleanup() override;

private:
    // 受信に利用する設定値を保持
    FileInputSettings settings_;
};

// シリアルポートからデータを受信するセッション
class SerialSession : public StreamingSession {
public:
    // シリアル設定と共有バッファを受け取って初期化
    SerialSession(const SerialInputSettings &settings, GlobalBuffer &buffer);

protected:
    // シリアルポート監視ループを実装
    void run() override;
    // ポートをクローズする後処理を実装
    void cleanup() override;

private:
    // 利用するシリアル設定を保持
    SerialInputSettings settings_;
    // OS 依存のハンドル値を保持
    std::intptr_t handle_{-1};
};

// TCP/UDP ソケットからデータを受信するセッション
class IpSession : public StreamingSession {
public:
    // ネットワーク設定と共有バッファを受け取って初期化
    IpSession(const IpInputSettings &settings, GlobalBuffer &buffer);

protected:
    // ソケットを開いてデータを受信する処理を実装
    void run() override;
    // ソケット破棄や WinSock 後処理を実装
    void cleanup() override;

private:
    // ネットワーク接続の設定
    IpInputSettings settings_;
    // ソケットのハンドル値
    std::intptr_t socketHandle_{-1};
#ifdef _WIN32
    // WinSock を初期化したかどうか
    bool wsaInitialized_{false};
#endif
};

using StreamingSessionPtr = std::unique_ptr<StreamingSession>;

} // namespace framework4cpp

