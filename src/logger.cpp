//
// Created by Sanger Steel on 5/22/25.
//

#include "logger.hpp"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>

thread_local std::vector<time_point> AsyncLogger::time_starts;

AsyncLogger::AsyncLogger(const std::string& filename) : done(false) {
    if (filename == "stdout") {
        fd = STDOUT_FILENO;
    } else {
        fd = open("log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            perror("open");
            exit(1);
        }
    }
    // Deploy logger thread
    logger = std::thread(&AsyncLogger::display_loop, this);
}

AsyncLogger::~AsyncLogger() { {
        std::lock_guard<std::mutex> lock(mu);
        done = true;
        ready_to_read = true;
    }
    cv.notify_one(); // Make sure the display loop isn't stuck waiting to be able to query `done`
    logger.join();
    if (fd != STDOUT_FILENO) {
        close(fd);
    }
}

void AsyncLogger::write(std::string message) {
    messages.push(std::move(message));

    // Multiple producers (possible log writers), so ready_to_read can risk data races,
    // which is why it's atomic here
    ready_to_read.store(true, std::memory_order_release);
    cv.notify_one();
}

size_t LoggingContext::set_start() {
    if (DEBUG) {
        if (logger.time_starts.size() > 10000) {
            logger.time_starts.clear();
            logger.time_starts.shrink_to_fit();
        }
        auto time_start = std::chrono::high_resolution_clock::now();
        logger.time_starts.emplace_back(time_start);
        return logger.time_starts.size() - 1;
    }
}

void LoggingContext::set_stop_and_display_time(size_t idx, const char* name) {
    if (DEBUG) {
        auto end_minus_start = std::chrono::high_resolution_clock::now() - logger.time_starts[idx];
        auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(end_minus_start).count();
        auto msg = std::format("{} took {} s", name, duration);
        write(msg.c_str());
    }
}

LoggingContext::LoggingContext(const std::string& filename, LogLevel level) : logger(filename), level(level) {
}

void LoggingContext::write(std::string message) {
    if (level == DEBUG) {
        logger.write(std::move(message));
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
            if (to_display.content.empty()) {
                throw std::runtime_error("Fail");
            }
            auto to_write = std::format("DEBUG: {}\n", to_display.content);
            ::write(fd, to_write.c_str(), to_write.size());
        }
    }
}

