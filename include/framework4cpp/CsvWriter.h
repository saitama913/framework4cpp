#pragma once

#include "framework4cpp/Config.h"
#include "framework4cpp/GlobalBuffer.h"

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace framework4cpp {

class CsvWriter {
public:
    CsvWriter(const CsvSettings &settings, GlobalBuffer &buffer);
    ~CsvWriter();

    void start();
    void stop();

private:
    void run();
    std::string formatRecord(const BufferItem &item) const;
    static std::string escape(const std::string &value);

    CsvSettings settings_;
    GlobalBuffer &buffer_;
    std::ofstream output_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex fileMutex_;
};

} // namespace framework4cpp

