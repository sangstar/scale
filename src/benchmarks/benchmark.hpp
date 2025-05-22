//
// Created by Sanger Steel on 5/6/25.
//

#pragma once
#include <string>
#include <vector>
#include "../../external/json.hpp"
#include "../curl.hpp"
#include "../request_parameters.hpp"
#include "../ring_buffers.hpp"
#include "labels.hpp"
#include <atomic>
#include <fstream>
#include <cctype>
#include "../results.hpp"

constexpr int NumWorkersPerRequest = 8;
constexpr int NumConcurrentRequests = 40;

using json = nlohmann::json;


enum RowDatatypes {
    STRING,
    INTEGER,
};

enum AnswerType {
    MultipleChoice,
    YesOrNo,
};

template <typename T>
concept Benchmark = requires(T benchmark, json& row)
{
    { benchmark.request_from_dataset_row(row) } -> std::same_as<RequestParameters>;
    { benchmark.pre_formatted_text } -> std::same_as<const std::string_view&>;
    { benchmark.dataset } -> std::same_as<json&>;
    { benchmark.label_map } -> std::convertible_to<LabelStatesMapping*>;
};

template <Benchmark B>
LabelStates get_label_state(const B& bench, const RequestParameters& req) {
    auto label = req.golden_label;
    for (auto [k, v] : bench.label_map) {
        if (label == k) {
            return v;
        }
    }
    return NO_LABEL;
}

bool guessed_correctly(LabelStates state, const Results& res);

void report_results(FinalMetrics& metrics);

template <Benchmark B>
std::vector<json> get_output_json(const Results& res, const B& bench) {
    std::vector<json> json_vec;
    auto state = get_label_state(bench, res.params);
    auto correct = guessed_correctly(state, res);
    for (const auto& compl_result: res.completion_results) {
        json j = json::object();
        j["e2e_latency"] = res.latencies.end_to_end_latency.count();
        j["ttft"] = res.latencies.ttft.count();
        j["id"] = compl_result.id;
        j["model"] = compl_result.model;
        j["object"] = compl_result.object;
        j["prompt"] = res.params.prompt;
        j["guessed_correctly"] = correct;
        int choice_count = 0;
        for (auto& choice : compl_result.choices) {
            std::string choice_id = "choice_" + std::to_string(choice_count);
            j[choice_id + "_finish_reason"] = choice.finish_reason;
            j[choice_id + "_text"] = choice.text;
        }
        json_vec.emplace_back(j);
    }
    return std::move(json_vec);
};





template <Benchmark Bench>
struct BenchmarkContext {
    Bench benchmark;
    bool finished;
    std::shared_ptr<MPSCRingBuffer<Results>> results_buffer;
    std::shared_ptr<CURLHandler> shared_client;
    BenchmarkContext(Bench bench, std::shared_ptr<CURLHandler> shared_client) : benchmark(bench), finished(false),
        shared_client(shared_client) {
        results_buffer = std::make_shared<MPSCRingBuffer<Results>>();
    }

    void perform_benchmark(const char* filename_jsonl, LoggingContext* maybe_logger) {


        FinalMetrics metrics;
        metrics.output_jsonl = filename_jsonl;
        metrics.requests_processed = 0;
        if (maybe_logger) {
            metrics.logger = maybe_logger;
        }
        metrics.benchmark_start = std::chrono::high_resolution_clock::now();


        auto writer_closure = [this, &metrics] {
            consume_buffer_and_write_to_json(metrics);
        };
        std::thread writer(writer_closure);
        std::atomic<int> job_id = 0;

        auto request_worker_closure = [&job_id, this] {
            while (true) {
                auto idx = job_id.fetch_add(1, std::memory_order_acquire);

                if (idx >= this->benchmark.size()) {
                    break;
                }

                auto params = this->benchmark.request_from_dataset_row(idx);
                send_and_add_to_buffer(params);
            }

        };

        std::vector<std::thread> workers;
        for (int i = 0; i < NumConcurrentRequests; ++i) {
            std::thread t(request_worker_closure);
            workers.emplace_back(std::move(t));
        }
        for (int i = 0; i < NumConcurrentRequests; ++i) {
            workers[i].join();
        }
        finished = true;

        writer.join();
    }

    void consume_buffer_and_write_to_json(FinalMetrics& metrics) const {
        std::ofstream outfile(metrics.output_jsonl);

        if (!outfile.is_open()) {
            throw std::runtime_error("failed to open output file");
        }

        // With many producers, it's unlikely for this thread to be so quick
        // that it finishes early due to an empty buffer before all producers
        // are finished, so we can probably get away with not signaling done
        while (true) {
            RingResult<Results> result = this->results_buffer->fetch();
            if (result.state == SUCCESS) {
                metrics.requests_processed++;
                auto content = result.content.value();
                auto jsons = get_output_json<Bench>(content, this->benchmark);
                for (auto& jsonl : jsons) {
                    auto jsonl_str = jsonl.dump();
                    metrics.logger->write(std::format("Req {}:, {}", metrics.requests_processed, jsonl.dump()).c_str());
                    outfile << jsonl_str << '\n';
                }
            }
            if (result.state == EMPTY && this->finished) {
                break;
            }
        }
        metrics.benchmark_end = std::chrono::high_resolution_clock::now();
        report_results(metrics);
        outfile.close();
    }

    BenchmarkContext(Bench bench, const char* uri) : benchmark(bench), finished(false) {
        shared_client = std::make_shared<CURLHandler>(uri, std::getenv("OPENAI_API_KEY"));
        results_buffer = std::make_shared<MPSCRingBuffer<Results>>();
    }

    void send_and_add_to_buffer(RequestParameters& req) {
        auto res = std::make_shared<std::vector<CompletionResults>>();
        const auto id = this->shared_client->post_stream(req);
        std::mutex res_mutex;


        int max_retries = 2;
        auto closure = [this, id, res, &res_mutex, max_retries] {
            int retries = 0;
            while (true) {
                bool producer_finished = this->shared_client->write_to_buffer_finished(id);
                auto val = this->shared_client->fetch(id);
                if (val.state == SUCCESS) {
                    bool fine_to_add = true;
                    CompletionResults results(val.content.value());

                    // For finish_reason = length, an empty response
                    // is thrown back at the end indicating it reached
                    // the finish_reason. Don't include these results.
                    // TODO: The logic here is likely flimsy and it assumes
                    //       choices has only one element (which is all I'm
                    //       used to seeing and assuming). What if there are
                    //       multiple Choices and one has text = "" and one doesn't?
                    //       this would disqualify all

                    for (auto& choice : results.choices) {
                        if (choice.text == "") {
                            fine_to_add = false;
                        }
                    }
                    // TODO: Make this atomic
                    if (fine_to_add) {
                        std::lock_guard<std::mutex> lock(res_mutex);
                        res->emplace_back(std::move(results));
                    }
                } else if (val.state == EMPTY && producer_finished) {
                    if (max_retries == retries) {
                        break;
                    }
                    retries++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };

        std::vector<std::thread> workers;
        for (int i = 0; i < NumWorkersPerRequest; ++i) {
            std::thread t(closure);
            workers.emplace_back(std::move(t));
        }
        auto latencies = shared_client->await(id);
        for (int i = 0; i < NumWorkersPerRequest; ++i) {
            workers[i].join();
        }

        if (res->size() > 0) {
            Results result;

            // res doesn't need to be shared anymore, so dereference off
            // the shared_ptr safeguard and move the result for the caller
            // to own it.
            result.completion_results = std::move(*res);
            result.latencies = latencies;
            result.params = req;

            results_buffer->push(result);
        }
        else {
            Logger.write("Worker found no jobs from request buffer.");
        }
    }
};




