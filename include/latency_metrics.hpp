//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <chrono>

using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct LatencyMetrics {
    std::chrono::duration<double, std::milli> ttft;
    std::chrono::duration<double, std::milli> end_to_end_latency;
};

