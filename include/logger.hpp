//
// Created by Sanger Steel on 5/22/25.
//

#pragma once
#include <mutex>
#include <condition_variable>
#include <thread>
#include "ring_buffers.hpp"
#include "latency_metrics.hpp"

enum LogLevel {
    NOTSET,
    INFO,
    DEBUG,
    ERROR,
};

class AsyncLogger {
public:
    void display_loop();

    explicit AsyncLogger(const std::string& filename);

    ~AsyncLogger();

    void write(std::string message);

    static thread_local std::vector<time_point> time_starts;

private:
    MPSCRingBuffer<std::string> messages;
    int fd;
    std::condition_variable cv;
    std::mutex mu;
    bool done;
    std::atomic<bool> ready_to_read;
    std::thread logger;
    std::ostream* stream;
};

struct LoggingContext {
    LogLevel level;
    AsyncLogger logger;

    LoggingContext(const std::string& filename, LogLevel level);

    ~LoggingContext() = default;

    void debug(std::string message);

    void debug(std::string message_fmt, std::string message);

    void info(std::string message);

    void error(std::string message) const;

    size_t set_start();

    void set_stop_and_display_time(size_t id, const char* name);
};


extern LoggingContext Logger;
