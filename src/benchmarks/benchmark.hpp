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

constexpr int NumWorkersPerRequest = 2;
constexpr int NumWorkers = 2;

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


template <Benchmark B>
std::vector<json> get_output_json(const Results& res, const B& bench) {
    std::vector<json> json_vec;
    auto state = get_label_state(bench, res.params);
    auto correct = guessed_correctly(state, res);
    for (const auto& compl_result: res.completion_results) {
        json j;
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
    std::shared_ptr<MPSCRingBuffer> results_buffer;
    std::shared_ptr<CURLHandler> shared_client;
    BenchmarkContext(Bench bench, std::shared_ptr<CURLHandler> shared_client) : benchmark(bench), finished(false),
        shared_client(shared_client) {
        results_buffer = std::make_shared<MPSCRingBuffer>();
    }

    void perform_benchmark(const char* filename_jsonl) {
        auto writer_closure = [=] {
            consume_buffer_and_write_to_json(filename_jsonl);
        };
        std::thread writer(writer_closure);

        std::atomic<int> job_id = 0;

        auto request_worker_closure = [&job_id, this] {
            while (true) {
                auto idx = job_id.fetch_add(1, std::memory_order_acquire);
                // Should be condition if (idx >= this->benchmark.size()), limiting this
                // so I don't run out of credits lol
                //if (idx >= this->benchmark.size()) {
                if (idx >= 5) {
                    break;
                }
                // TODO: I have the benchmark class in scope and the full informative response struct
                //       here. This would be a great place to actually grade the response here by comparing
                //       the "golden label" for this row. Should be an easy way to score.
                //       something like:
                //       ```
                //       auto params_and_golden_label = this->benchmark.request_and_label_from_dataset_row(idx);
                //       auto label = std::get<1>(params_and_golden_label)
                //       auto params = std::get<0>(params_and_golden_label)
                //       send_and_add_to_buffer(params, label);
                //       ```
                //       Where I can modify send_and_add_to_buffer() to include the `label` part and
                //       can modify the `Results` struct to include the label, and then the writer thread
                //       can access if the answer was correct by comparing the text generated and label
                auto params = this->benchmark.request_from_dataset_row(idx);
                send_and_add_to_buffer(params);
            }

        };

        std::vector<std::thread> workers;
        for (int i = 0; i < NumWorkers; ++i) {
            std::thread t(request_worker_closure);
            workers.emplace_back(std::move(t));
        }
        for (int i = 0; i < NumWorkers; ++i) {
            workers[i].join();
        }
        finished = true;

        writer.join();
    }

    void consume_buffer_and_write_to_json(const char* filename_jsonl) const {
        std::ofstream outfile(filename_jsonl);

        if (!outfile.is_open()) {
            throw std::runtime_error("failed to open output file");
        }

        // With many producers, it's unlikely for this thread to be so quick
        // that it finishes early due to an empty buffer before all producers
        // are finished, so we can probably get away with not signaling done
        while (true) {
            RingResult<Results> result = this->results_buffer->fetch();
            if (result.state == SUCCESS) {
                auto content = result.content.value();
                auto jsons = get_output_json<Bench>(content, this->benchmark);
                for (auto& jsonl : jsons) {
                    outfile << jsonl.dump() << '\n';
                }
            }
            if (result.state == EMPTY && this->finished) {
                break;
            }
        }
        outfile.close();
    }

    BenchmarkContext(Bench bench, const char* uri) : benchmark(bench), finished(false) {
        shared_client = std::make_shared<CURLHandler>(uri, std::getenv("OPENAI_API_KEY"));
        results_buffer = std::make_shared<MPSCRingBuffer>();
    }

    void send_and_add_to_buffer(RequestParameters& req) {
        auto res = std::make_shared<std::vector<CompletionResults>>();
        const auto id = this->shared_client->post_stream(req);
        std::mutex res_mutex;


        auto closure = [this, id, res, &res_mutex] {
            while (true) {
                auto val = this->shared_client->fetch(id);
                if (val.state == SUCCESS) {
                    bool fine_to_add = true;
                    CompletionResults results(val.content.value());
                    // For finish_reason = length, an empty response
                    // is thrown back at the end indicating it reached
                    // the finish_reason. Don't include these results.
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
                } else if (val.state == EMPTY && this->shared_client->write_to_buffer_finished(id)) {
                    break;
                }
                else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
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

        Results result;

        // res doesn't need to be shared anymore, so dereference off
        // the shared_ptr safeguard and move the result for the caller
        // to own it.
        result.completion_results = std::move(*res);
        result.latencies = latencies;
        result.params = req;

        results_buffer->push(result);
    }
};




