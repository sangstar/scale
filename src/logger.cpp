//
// Created by Sanger Steel on 5/22/25.
//

#include "logger.hpp"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>


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

void AsyncLogger::write(const char* message) {
    auto as_str = std::string(message);
    messages.push(as_str);

    // Multiple producers (possible log writers), so ready_to_read can risk data races,
    // which is why it's atomic here
    ready_to_read.store(true, std::memory_order_release);
    cv.notify_one();
}

LoggingContext::LoggingContext(const std::string& filename, LogLevel level) : logger(filename), level(level) {
}

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
        std::string* to_display;
        auto state = messages.fetch(to_display);
        if (state == SUCCESS) {
            auto to_write = std::format("DEBUG: {}\n", *to_display);
            ::write(fd, to_write.c_str(), to_write.size());
        }
    }
}

