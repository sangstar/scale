//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include "request_parameters.hpp"
#include "completion_types.hpp"
#include "latency_metrics.hpp"

struct Results {
    RequestParameters params;
    std::vector<CompletionResults> completion_results;
    LatencyMetrics latencies;
    std::vector<json> to_json();
};
