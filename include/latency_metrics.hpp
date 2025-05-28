//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <chrono>

using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct LatencyMetrics {
    double ttft;
    double end_to_end_latency;
};

