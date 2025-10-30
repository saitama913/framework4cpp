#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace framework4cpp {

// スレッド関連の共通設定を保持する構造体
struct ThreadingSettings {
    // I/O 処理に割り当てるワーカースレッド数
    std::size_t ioThreadCount{1};
};

// グローバルバッファに関する設定を保持する構造体
struct BufferSettings {
    // バッファに保持できる最大アイテム数
    std::size_t capacity{1024};
    // 各アイテムに許可する最大ペイロードサイズ
    std::size_t maxPayloadSize{4096};
    // メモリマップトファイルを用いてバッファを管理するかどうか
    bool memoryMapped{false};
    // メモリマップトファイルを利用する際のバックファイルパス
    std::string backingFile{};
};

// CSV 出力の整形と出力方法に関する設定
struct CsvSettings {
    // 出力する CSV ファイルのパス
    std::string outputPath{"output.csv"};
    // CSV の区切り文字
    char delimiter{','};
    // 文字列を引用符で囲むかどうか
    bool quoteStrings{true};
    // タイムスタンプ列を含めるかどうか
    bool includeTimestamp{true};
    // 出力バッファをフラッシュする間隔
    std::chrono::milliseconds flushInterval{std::chrono::milliseconds{1000}};
    // タイムスタンプ整形に使用するフォーマット文字列
    std::string timestampFormat{"%Y-%m-%d %H:%M:%S"};
};

// ファイル入力を制御するための設定
struct FileInputSettings {
    // ファイル入力機能を有効にするかどうか
    bool enabled{false};
    // 監視するファイルのパス
    std::string path{};
    // ファイルの追尾（tail -f のような動作）を行うかどうか
    bool follow{false};
    // 1 回の読み取りで確保するバイト数
    std::size_t readChunkSize{4096};
    // 追尾時にポーリングする間隔
    std::chrono::milliseconds pollInterval{std::chrono::milliseconds{200}};
};

// シリアルポート入力に関する設定
struct SerialInputSettings {
    // シリアル入力機能を有効にするかどうか
    bool enabled{false};
    // オープンするシリアルポート名
    std::string port{};
    // シリアルポートのボーレート
    unsigned int baudRate{9600};
    // 読み取りバッファのサイズ
    std::size_t readChunkSize{256};
};

// IP（TCP/UDP）入力に関する設定
struct IpInputSettings {
    // ネットワーク入力機能を有効にするかどうか
    bool enabled{false};
    // 接続・バインドするホスト名または IP アドレス
    std::string host{"127.0.0.1"};
    // 接続・バインドするポート番号
    std::uint16_t port{0};
    // UDP で接続する場合は true、TCP の場合は false
    bool udp{false};
    // 受信時に利用するバッファサイズ
    std::size_t readChunkSize{512};
};

class Config {
public:
    // スレッド設定をまとめた構造体
    ThreadingSettings threading;
    // グローバルバッファ関連の設定
    BufferSettings buffer;
    // CSV 出力関連の設定
    CsvSettings csv;
    // ファイル入力の設定
    FileInputSettings fileInput;
    // シリアル入力の設定
    SerialInputSettings serialInput;
    // IP 入力の設定
    IpInputSettings ipInput;

    // 指定されたパスから設定ファイルを読み込み、Config を構築する
    static Config loadFromFile(const std::string &path);

private:
    // 文字列の前後空白を除去するユーティリティ
    static std::string trim(const std::string &value);
    // 真偽値を表す文字列を bool に変換する
    static bool parseBool(const std::string &value);
    // ミリ秒表現を std::chrono::milliseconds に変換する
    static std::chrono::milliseconds parseDurationMs(const std::string &value);
    // サイズ表現を std::size_t に変換する（単位付き対応）
    static std::size_t parseSize(const std::string &value);
    // 非負整数表現を unsigned int に変換する
    static unsigned int parseUnsigned(const std::string &value);
    // ポート番号表現を std::uint16_t に変換する
    static std::uint16_t parsePort(const std::string &value);
};

} // namespace framework4cpp

