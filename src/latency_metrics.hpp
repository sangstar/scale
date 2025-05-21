//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <chrono>

struct LatencyMetrics {
    std::chrono::duration<double, std::milli> ttft;
    std::chrono::duration<double, std::milli> end_to_end_latency;
};

