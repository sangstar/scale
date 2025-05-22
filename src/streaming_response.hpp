//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include "ring_buffers.hpp"
#include "latency_metrics.hpp"
#include <thread>

struct StreamingResponse {
    bool got_ttft;
    time_point start;
    LatencyMetrics latencies;
    SPMCRingBuffer ring;
    std::thread t;
    bool done;
};
