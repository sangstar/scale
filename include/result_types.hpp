//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include "request_parameters.hpp"
#include "completion_types.hpp"
#include "latency_metrics.hpp"
#include "logger.hpp"

struct RequestResult {
    RequestParameters params;
    std::vector<CompletionResults> completion_results;
    bool guessed_correctly;
    LatencyMetrics latencies;
    std::vector<json> to_json();
};


// TODO: Will use this in an array/vector to aggregate stats over all
//       requests to get averages for these times, and also stuff like
//       F1 etc for yes/no guesses
struct ParsedRequestResult {
    float e2e_latency;
    float ttft;
    std::string id;
    std::string model;
    std::string object;
    std::string prompt;
    bool guessed_correctly;
    float yes_logprob;
    float no_logprob;
    std::string finish_reason;
    std::string text;

    static void from_json(const json& j, ParsedRequestResult& b) {
        j.at("e2e_latency").get_to(b.e2e_latency);
        j.at("ttft").get_to(b.ttft);
        j.at("id").get_to(b.id);
        j.at("model").get_to(b.model);
        j.at("object").get_to(b.object);
        j.at("prompt").get_to(b.prompt);
        j.at("guessed_correctly").get_to(b.guessed_correctly);
        j.at("yes_logprob").get_to(b.yes_logprob);
        j.at("no_logprob").get_to(b.no_logprob);
        j.at("finish_reason").get_to(b.finish_reason);
        j.at("text").get_to(b.text);
    }
};

struct Metrics {
    const char* output_jsonl;
    explicit Metrics(const char* jsonl) : output_jsonl(jsonl) {};
    time_point benchmark_start = std::chrono::high_resolution_clock::now();
    time_point benchmark_end;
    double requests_processed = 0;
    std::vector<RequestResult> req_results;
};

struct FinalMetrics {
    double avg_ttft;
    double avg_e2e_latency;
    double requests_processed;
    double duration;
    double req_rate;
    double accuracy;
    std::string display();
};
