//
// Created by Sanger Steel on 5/22/25.
//

#include "logger.hpp"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <execinfo.h>

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
    if (this->level == DEBUG) {
        if (logger.time_starts.size() > 10000) {
            logger.time_starts.clear();
            logger.time_starts.shrink_to_fit();
        }
        auto time_start = std::chrono::high_resolution_clock::now();
        logger.time_starts.emplace_back(time_start);
        return logger.time_starts.size() - 1;
    }
    return 0;
}

void LoggingContext::set_stop_and_display_time(size_t idx, const char* name) {
    if (this->level == DEBUG) {
        auto end_minus_start = std::chrono::high_resolution_clock::now() - logger.time_starts[idx];
        auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(end_minus_start).count();
        auto msg = std::format("DEBUG: {} took {} s", name, duration);
        logger.write(msg);
    }
}

LoggingContext::LoggingContext(const std::string& filename, LogLevel level) : logger(filename), level(level) {
}

void LoggingContext::debug(std::string message) {
    if (level == DEBUG) {
        logger.write(std::format("DEBUG: {}", std::move(message)));
    }
};

void LoggingContext::debug(std::string message_fmt, std::string message) {
    auto msg = std::vformat(message_fmt, std::make_format_args(message));
    if (level == DEBUG) {
        logger.write(std::format("DEBUG: {}", msg));
    }
};

void LoggingContext::info(std::string message) {
    if (level >= INFO) {
        logger.write(std::format("INFO: {}", std::move(message)));
    }
}

void LoggingContext::error(std::string message) const {
    if (level >= ERROR) {
        throw std::runtime_error(std::format("ERROR: {}", std::move(message)));
    }
};

void AsyncLogger::display_loop() {
    while (!done) {
        {
            std::unique_lock<std::mutex> wait_response_lock(mu);
            cv.wait(wait_response_lock, [this] { return ready_to_read.load(std::memory_order_acquire) || done; });
        }
        std::string msg;
        auto to_display = messages.fetch();
        if (to_display.state == RingState::SUCCESS) {
            msg = to_display.content.value();
            auto to_write = std::format("{}\n", msg);
            ::write(fd, to_write.c_str(), to_write.size());
        }
    }
}

