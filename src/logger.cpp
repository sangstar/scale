//
// Created by Sanger Steel on 5/22/25.
//

#include "logger.hpp"
#include <iostream>



AsyncLogger::AsyncLogger(const std::string& filename) {
    if (filename == "stdout") {
        stream = &std::cout;
    } else {
        owned_log_file.open(filename);
        if (!owned_log_file.is_open()) {
            throw std::runtime_error("Failed to open log file: " + filename);
        }
        stream = &owned_log_file;
    }

    // Deploy logger thread
    logger = std::thread(&AsyncLogger::display_loop, this);
}

AsyncLogger::~AsyncLogger() {
    {
        std::lock_guard<std::mutex> lock(mu);
        done = true;
        ready_to_read = true;
    }
    cv.notify_one(); // Make sure the display loop isn't stuck waiting to be able to query `done`
    logger.join();
}

void AsyncLogger::write(const char* message) {
    auto as_str = std::string(message);
    messages.push(as_str);

    // Multiple producers (possible log writers), so ready_to_read can risk data races,
    // which is why it's atomic here
    ready_to_read.store(true, std::memory_order_release);
    cv.notify_one();
}

LoggingContext::LoggingContext(const std::string& filename, LogLevel level) : logger(filename), level(level) {}

void LoggingContext::write(const char* message) {
    if (level == DEBUG) {
        logger.write(message);
    }
};

void AsyncLogger::display_loop() {
    while (!done) {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [this] { return ready_to_read.load(std::memory_order_acquire) || done; });
        if (done) {
            break;
        }
        auto to_display = messages.fetch();
        if (to_display.state == SUCCESS) {
            std::cout << "DEBUG: " << to_display.content.value() << std::endl;
        }
    }
}

