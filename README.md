# framework4cpp

フレームワーク化を進めるための実験的なストリーミング基盤です。設定ファイルで入出力やスレッド構成を切り替えられ、共通リングバッファに集約したデータをCSVとして書き出します。

## ビルド方法

現状はビルドスクリプトを同梱していないため、任意のC++17対応コンパイラでソースを直接コンパイルしてください。以下は Linux/macOS で `g++` を使う例です。

```bash
g++ -std=c++17 -O2 -pthread \
    -Iinclude \
    app/main.cpp \
    src/config/Config.cpp \
    src/core/GlobalBuffer.cpp \
    src/io/CsvWriter.cpp \
    src/streaming/FileSession.cpp \
    src/streaming/SerialSession.cpp \
    src/streaming/IpSession.cpp \
    -o framework4cpp
```

Windows で MinGW を利用する場合も概ね同様です。MSVC を使用する場合はソリューションを作成し、同じソースファイルを追加してください。

## 設定ファイル (`config.ini`)

設定は INI 形式で記述します。主なセクションとキーは以下の通りです。

```ini
[common]
# I/O セッションで利用するスレッド数
io_thread_count = 2

[buffer]
# リングバッファの要素数と1エントリ当たりの最大バイト数
capacity = 4096
max_payload_size = 8192
# メモリマップトファイルを使う場合は true にし、バックファイルを指定します
memory_mapped = false
backing_file = buffer.dat

[csv]
output_path = output/data.csv
delimiter = ,
quote_strings = true
include_timestamp = true
flush_interval_ms = 1000
timestamp_format = %Y-%m-%d %H:%M:%S

[file_input]
enabled = true
path = /path/to/input.log
follow = true
read_chunk_size = 4096
poll_interval_ms = 200

[serial_input]
enabled = false
port = COM3
baud_rate = 115200
read_chunk_size = 256

[ip_input]
enabled = false
host = 127.0.0.1
port = 9000
udp = false
read_chunk_size = 512
```

数値キーには `64k` や `8mb` のような接尾辞を付けることもできます。真偽値は `true/false`, `on/off` などを受け付けます。

## 実行方法

```bash
./framework4cpp config.ini
```

- 引数を省略するとカレントディレクトリの `config.ini` を読み込みます。
- `Ctrl+C` などで `SIGINT` / `SIGTERM` を送るか、標準入力で Enter を押すとクリーンに終了します。
- 有効化した各セッション（ファイル監視、シリアル、TCP/UDP）が非同期に受信したデータを共有バッファへ投入し、`CsvWriter` が一定周期で CSV へフラッシュします。

## ライセンス

現時点では未定義です。必要に応じて追記してください。
