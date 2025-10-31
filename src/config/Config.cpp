#include "framework4cpp/Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace framework4cpp {
namespace {

enum class Section {
    None,
    Common,
    Buffer,
    Csv,
    FileInput,
    SerialInput,
    IpInput
};

// セクション名を列挙値へ変換するマップを構築する
std::unordered_map<std::string, Section> buildSectionMap() {
    return {
        {"common", Section::Common},
        {"buffer", Section::Buffer},
        {"csv", Section::Csv},
        {"file_input", Section::FileInput},
        {"serial_input", Section::SerialInput},
        {"ip_input", Section::IpInput}
    };
}

} // namespace

Config Config::loadFromFile(const std::string &path) {
    // 設定ファイルを開いて読み込みを準備する
    std::ifstream file(path);
    if (!file.is_open()) {
        // ファイルが開けなかった場合は例外を投げる
        throw std::runtime_error("Failed to open config file: " + path);
    }

    // 読み取った値を格納する Config インスタンス
    Config config;
    // 現在解析中のセクション種別
    Section currentSection = Section::None;
    // セクション名の解決に利用するマップ
    const auto sectionMap = buildSectionMap();

    std::string line;
    while (std::getline(file, line)) {
        // 行の前後空白を削除して評価する
        line = trim(line);
        // 空行とコメント行は無視する
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // セクション定義 ([section]) を検出した場合の処理
        if (line.front() == '[' && line.back() == ']') {
            // セクション名の抽出と正規化
            std::string sectionName = trim(line.substr(1, line.size() - 2));
            auto lowerSection = sectionName;
            std::transform(lowerSection.begin(), lowerSection.end(), lowerSection.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            // マップからセクションを検索
            auto it = sectionMap.find(lowerSection);
            if (it == sectionMap.end()) {
                throw std::runtime_error("Unknown config section: " + sectionName);
            }
            // 現在のセクションを更新して次の行へ
            currentSection = it->second;
            continue;
        }

        // キーと値の区切り文字 '=' を探索
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            throw std::runtime_error("Invalid config line: " + line);
        }

        // '=' の左右をキーと値として切り出す
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        // キーは小文字に統一して扱う
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        // 現在のセクションに応じて値を設定する
        switch (currentSection) {
        case Section::Common:
            if (key == "io_thread_count") {
                config.threading.ioThreadCount = parseSize(value);
            } else {
                throw std::runtime_error("Unknown key in [common]: " + key);
            }
            break;
        case Section::Buffer:
            if (key == "capacity") {
                config.buffer.capacity = parseSize(value);
            } else if (key == "max_payload_size") {
                config.buffer.maxPayloadSize = parseSize(value);
            } else if (key == "memory_mapped") {
                config.buffer.memoryMapped = parseBool(value);
            } else if (key == "backing_file") {
                config.buffer.backingFile = value;
            } else if (key == "source_field") {
                // 発生元フィールド名の上書き指定
                config.buffer.fieldNames.source = value;
            } else if (key == "timestamp_field") {
                // タイムスタンプフィールド名の上書き指定
                config.buffer.fieldNames.timestamp = value;
            } else if (key == "payload_field") {
                // ペイロードフィールド名の上書き指定
                config.buffer.fieldNames.payload = value;
            } else {
                throw std::runtime_error("Unknown key in [buffer]: " + key);
            }
            break;
        case Section::Csv:
            if (key == "output_path") {
                config.csv.outputPath = value;
            } else if (key == "delimiter") {
                config.csv.delimiter = value.empty() ? ',' : value.front();
            } else if (key == "quote_strings") {
                config.csv.quoteStrings = parseBool(value);
            } else if (key == "include_timestamp") {
                config.csv.includeTimestamp = parseBool(value);
            } else if (key == "flush_interval_ms") {
                config.csv.flushInterval = parseDurationMs(value);
            } else if (key == "timestamp_format") {
                config.csv.timestampFormat = value;
            } else {
                throw std::runtime_error("Unknown key in [csv]: " + key);
            }
            break;
        case Section::FileInput:
            if (key == "enabled") {
                config.fileInput.enabled = parseBool(value);
            } else if (key == "path") {
                config.fileInput.path = value;
            } else if (key == "follow") {
                config.fileInput.follow = parseBool(value);
            } else if (key == "read_chunk_size") {
                config.fileInput.readChunkSize = parseSize(value);
            } else if (key == "poll_interval_ms") {
                config.fileInput.pollInterval = parseDurationMs(value);
            } else {
                throw std::runtime_error("Unknown key in [file_input]: " + key);
            }
            break;
        case Section::SerialInput:
            if (key == "enabled") {
                config.serialInput.enabled = parseBool(value);
            } else if (key == "port") {
                config.serialInput.port = value;
            } else if (key == "baud_rate") {
                config.serialInput.baudRate = parseUnsigned(value);
            } else if (key == "read_chunk_size") {
                config.serialInput.readChunkSize = parseSize(value);
            } else {
                throw std::runtime_error("Unknown key in [serial_input]: " + key);
            }
            break;
        case Section::IpInput:
            if (key == "enabled") {
                config.ipInput.enabled = parseBool(value);
            } else if (key == "host") {
                config.ipInput.host = value;
            } else if (key == "port") {
                config.ipInput.port = parsePort(value);
            } else if (key == "udp") {
                config.ipInput.udp = parseBool(value);
            } else if (key == "read_chunk_size") {
                config.ipInput.readChunkSize = parseSize(value);
            } else {
                throw std::runtime_error("Unknown key in [ip_input]: " + key);
            }
            break;
        case Section::None:
            // セクション外でキーが定義された場合はエラーにする
            throw std::runtime_error("Key defined outside of a section: " + key);
        }
    }

    return config;
}

std::string Config::trim(const std::string &value) {
    // 空白文字判定用のラムダを定義
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    // 先頭から空白でない位置を探す
    auto begin = std::find_if_not(value.begin(), value.end(), isSpace);
    // 末尾側から空白でない位置を探す
    auto end = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    if (begin >= end) {
        return {};
    }
    // 範囲を指定して新しい文字列を作成
    return std::string(begin, end);
}

bool Config::parseBool(const std::string &value) {
    if (value.empty()) {
        return false;
    }

    // 入力文字列を小文字へ変換する
    std::string lower;
    lower.resize(value.size());
    std::transform(value.begin(), value.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    // 代表的な真偽表現を判定する
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
        return false;
    }
    // 解釈できない値は例外にする
    throw std::runtime_error("Invalid boolean value: " + value);
}

std::chrono::milliseconds Config::parseDurationMs(const std::string &value) {
    // サイズパーサーを利用してミリ秒を取得する
    return std::chrono::milliseconds{static_cast<long long>(parseSize(value))};
}

std::size_t Config::parseSize(const std::string &value) {
    if (value.empty()) {
        throw std::runtime_error("Numeric value expected, got empty string");
    }
    // 数値部分をパースし、末尾の単位を確認する
    std::size_t idx = 0;
    std::size_t number = std::stoull(value, &idx, 10);
    if (idx < value.size()) {
        std::string suffix = trim(value.substr(idx));
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        // サフィックスに応じて倍率を変更する
        if (suffix == "k" || suffix == "kb") {
            number *= 1024ull;
        } else if (suffix == "m" || suffix == "mb") {
            number *= 1024ull * 1024ull;
        } else if (!suffix.empty()) {
            throw std::runtime_error("Unknown size suffix: " + suffix);
        }
    }
    return number;
}

unsigned int Config::parseUnsigned(const std::string &value) {
    // parseSize を利用してから unsigned int に丸める
    return static_cast<unsigned int>(parseSize(value));
}

std::uint16_t Config::parsePort(const std::string &value) {
    // 汎用のサイズパーサーで値を取得し、範囲を検証する
    unsigned long parsed = parseSize(value);
    if (parsed > 65535) {
        throw std::runtime_error("Port value out of range: " + value);
    }
    return static_cast<std::uint16_t>(parsed);
}

} // namespace framework4cpp

