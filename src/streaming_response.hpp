//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include "ring_buffers.hpp"
#include <thread>

using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct StreamingResponse {
    bool got_ttft;
    time_point start;
    LatencyMetrics latencies;
    SPMCRingBuffer ring;
    std::thread t;
    bool done;
};
