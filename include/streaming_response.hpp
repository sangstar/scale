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
    std::thread t;
    std::condition_variable cv;
    bool done;
    bool check_producer_finished();
    bool ready_to_fetch() const;
    void finalize();
    std::mutex mu;
    void push(std::string str);
    RingResult<std::string> fetch();

    struct Feedback {
        std::atomic<int> chunks_pushed = 0;
        std::vector<std::string> failed_to_parse_strings;

        void evaluate() {
            if (failed_to_parse_strings.size() > 0) {
                for (const auto& string : failed_to_parse_strings) {
                    std::cout << "Couldn't parse string: " << string << std::endl;
                }
                throw std::runtime_error("Could not parse response.");
            }
        }
    };

    Feedback feedback;

private:
    std::atomic<bool> fetchable;
    SPMCRingBuffer<std::string> ring;
};


