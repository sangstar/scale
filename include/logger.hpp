//
// Created by Sanger Steel on 5/22/25.
//

#pragma once
#include <mutex>
#include <condition_variable>
#include <thread>
#include "latency_metrics.hpp"
#include "ring_buffers.hpp"

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

    void dump_debugging_state();

    std::vector<std::string> failed_to_parse_strings;

    std::atomic<int> pushed_chunks = 0;

    std::atomic<int> num_requests_sent = 0;

    std::atomic<int> num_processed = 0;

    std::atomic<int> requests_sent_to_compl_buffer = 0;

    std::atomic<int> disallowed_requests = 0;

    std::atomic<int> fetched_requests = 0;

    std::atomic<int> fetch_attempts = 0;

    std::atomic<int> send_chunks_calls = 0;

    std::atomic<int> failed_send_and_add_to_buffer_calls = 0;

    std::atomic<int> send_add_to_buffer_calls = 0;
};


extern LoggingContext Logger;
