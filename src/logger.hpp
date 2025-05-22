//
// Created by Sanger Steel on 5/22/25.
//

#pragma once
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <thread>
#include "ring_buffers.hpp"

class AsyncLogger {
public:
    void display_loop();
    AsyncLogger(const std::string& filename);
    ~AsyncLogger();
    void write(const char* message);
private:
    MPSCRingBuffer<std::string> messages;
    std::condition_variable cv;
    std::mutex mu;
    bool done;
    std::atomic<bool> ready_to_read;
    std::thread logger;
    std::ostream* stream;
    std::ofstream owned_log_file;
};

struct LoggingContext {
    AsyncLogger logger;
    LoggingContext(const std::string& filename);
    ~LoggingContext() = default;
    void write(const char* message);
};

extern LoggingContext Logger;