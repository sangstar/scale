//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include "request_parameters.hpp"
#include "completion_types.hpp"
#include "latency_metrics.hpp"
#include "logger.hpp"

struct Results {
    RequestParameters params;
    std::vector<CompletionResults> completion_results;
    LatencyMetrics latencies;
    std::vector<json> to_json();
};

struct FinalMetrics {
    const char* output_jsonl;
    LoggingContext* logger;
    time_point benchmark_start;
    time_point benchmark_end;
    int requests_processed;
};
