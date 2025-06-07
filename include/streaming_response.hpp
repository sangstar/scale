//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include "ring_buffers.hpp"
#include "latency_metrics.hpp"
#include <thread>

class StreamingResponse {
public:
    bool got_ttft;
    time_point start;
    LatencyMetrics latencies;
    SPMCRingBuffer<std::string> ring;
    std::thread t;
    std::condition_variable cv;
    bool done;
    bool ready_to_fetch() const;
    std::mutex mu;
    void push(std::string str);
    RingState fetch(std::string*& item);
private:
    std::atomic<bool> fetchable;

};


