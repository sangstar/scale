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
#include "benchmark_concept.hpp"
#include "../result_types.hpp"
#include "utils.hpp"
#include "constants.hpp"


using json = nlohmann::json;

template<Benchmark B>
LabelStates get_label_state(const B& bench, const RequestParameters& req) {
    auto label = req.golden_label;
    for (auto [k, v]: bench.label_map) {
        if (label == k) {
            return v;
        }
    }
    return NO_LABEL;
}

template<Benchmark B>
std::vector<json> get_output_json(const RequestResult& res, const B& bench) {
    std::vector<json> json_vec;
    auto state = get_label_state(bench, res.params);
    auto correct = guessed_correctly(state, res);
    auto logprobs_for_yes_and_no = get_yes_no_logprobs(state, correct, res);
    for (const auto& compl_result: res.completion_results) {
        json j = json::object();
        j["e2e_latency"] = res.latencies.end_to_end_latency;
        j["ttft"] = res.latencies.ttft;
        j["id"] = compl_result.id;
        j["model"] = compl_result.model;
        j["object"] = compl_result.object;
        j["prompt"] = res.params.prompt;
        j["guessed_correctly"] = correct;
        if (logprobs_for_yes_and_no.yes.has_value()) {
            j["yes_logprob"] = logprobs_for_yes_and_no.yes.value().dump();
        } else {
            j["yes_logprob"] = "N/A";
        }
        if (logprobs_for_yes_and_no.no.has_value()) {
            j["no_logprob"] = logprobs_for_yes_and_no.no.value().dump();
        } else {
            j["no_logprob"] = "N/A";
        }
        auto choice = compl_result.choices[0];
        j["finish_reason"] = choice.finish_reason;
        j["text"] = choice.text;
        json_vec.emplace_back(j);
    }
    return std::move(json_vec);
};

template<Benchmark Bench>
struct BenchmarkContext {
    Bench benchmark;
    bool finished;
    std::shared_ptr<MPSCRingBuffer<RequestResult>> results_buffer;
    std::shared_ptr<CURLHandler> shared_client;

    BenchmarkContext(Bench bench, std::shared_ptr<CURLHandler> shared_client) : benchmark(bench), finished(false),
        shared_client(shared_client) {
        results_buffer = std::make_shared<MPSCRingBuffer<RequestResult>>();
    }

    // perform_benchmark does the following:
    // 1. Creates a FinalMetrics object to write results to
    // 2. Creates a writing thread that captures the metrics and
    //    self by pointer, and does `consume_buffer_and_write_to_json`,
    //    which looks for `this`'s `results_buffer` entries from the producer
    //    threads and writes the results to jsonl
    // 3. Creates `NumConcurrentRequests` workers that get a unique row index
    //    to grab from the dataset, marshal it in to a request, and send a request to
    //    the server, returning a shared pointer that exposes the underlying streaming response.
    //    One thread is responsible for pushing parsed responses to a buffer and
    //    NumWorkersPerRequest threads are spawned that consume responses, parse and collate
    //    them.
    // 4. When a `send_and_add_to_buffer` worker is finished, it finally pushes the result to
    //    the writing thread from step 2 which writes it to jsonl
    void perform_benchmark(const char* filename_jsonl, LoggingContext* maybe_logger) {
        FinalMetrics metrics;
        metrics.output_jsonl = filename_jsonl;
        metrics.requests_processed = 0;
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
        for (int i = 0; i < WorkerConstants::NumConcurrentRequests; ++i) {
            std::thread t(request_worker_closure);
            workers.emplace_back(std::move(t));
        }
        for (int i = 0; i < WorkerConstants::NumConcurrentRequests; ++i) {
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
        RequestResult* result;
        while (true) {
            auto state = this->results_buffer->fetch(result);
            if (state == SUCCESS) {
                metrics.requests_processed++;
                auto jsons = get_output_json<Bench>(*result, this->benchmark);
                for (auto& jsonl: jsons) {
                    auto jsonl_str = jsonl.dump();
                    Logger.info(std::format("Req {}: {}", metrics.requests_processed, jsonl.dump()).c_str());
                    outfile << jsonl_str << '\n';
                }
            }
            if (state == EMPTY && this->finished) {
                break;
            }
        }
        metrics.benchmark_end = std::chrono::high_resolution_clock::now();
        report_results(metrics);
        outfile.close();
    }

    BenchmarkContext(Bench bench, const char* uri) : benchmark(bench), finished(false) {
        shared_client = std::make_shared<CURLHandler>(uri, std::getenv("OPENAI_API_KEY"));
        results_buffer = std::make_shared<MPSCRingBuffer<RequestResult>>();
    }

    // send_and_add_to_buffer creates a shared pointer to a `CompletionResults` buffer,
    // sends the request to the server and parses the response with `post_stream`,
    // and defines a closure to be performed by NumWorkersPerRequest. `post_stream` spawns a
    // worker that receives responses from the server, parses them in to valid JSON chunks, and
    // pushes them to a shared_ptr to a `StreamingResponse` object that is returned by `post_stream`.

    // Consumer workers are spawned, each holding a reference to the shared_ptr where the parsed JSON
    // data is written to, fetch a JSON chunk atomically, process it, and push it to the `CompletionResults` buffer.
    void send_and_add_to_buffer(RequestParameters& req) {
        auto res = std::make_shared<std::vector<CompletionResults>>();
        const auto resp = this->shared_client->post_stream(req);
        std::mutex res_mutex;

        int max_retries = 2;
        auto closure = [this, &resp, res, &res_mutex, max_retries] {
            int retries = 0;
            std::string* json_str;
            while (true) {
                bool producer_finished = this->shared_client->write_to_buffer_finished(resp);
                auto state = this->shared_client->fetch(resp, json_str);
                if (state == SUCCESS) {
                    bool fine_to_add = true;
                    CompletionResults results(*json_str);

                    // For finish_reason = length, an empty response
                    // is thrown back at the end indicating it reached
                    // the finish_reason. Don't include these results.
                    // TODO: The logic here is likely flimsy and it assumes
                    //       choices has only one element (which is all I'm
                    //       used to seeing and assuming). What if there are
                    //       multiple Choices and one has text = "" and one doesn't?
                    //       this would disqualify all

                    for (auto& choice: results.choices) {
                        if (choice.text == "") {
                            fine_to_add = false;
                        }
                    }
                    // TODO: Make this atomic
                    if (fine_to_add) {
                        std::lock_guard<std::mutex> lock(res_mutex);
                        res->emplace_back(std::move(results));
                    }
                } else if (state == EMPTY && producer_finished) {
                    if (max_retries == retries) {
                        break;
                    }
                    retries++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };

        std::vector<std::thread> workers;
        for (int i = 0; i < WorkerConstants::NumWorkersPerRequest; ++i) {
            std::thread t(closure);
            workers.emplace_back(std::move(t));
        }
        auto latencies = shared_client->await(resp);
        for (int i = 0; i < WorkerConstants::NumWorkersPerRequest; ++i) {
            workers[i].join();
        }

        if (res->size() > 0) {
            RequestResult result;

            // res doesn't need to be shared anymore since no one is reading
            // or writing to it (no more surprises), so dereference off
            // the shared_ptr safeguard and move the result for the caller
            // to own it.
            result.completion_results = std::move(*res);
            result.latencies = latencies;
            result.params = req;

            results_buffer->push(result);
        } else {

            // If the worker found no work to be processed from the stream buffer, just make a note of
            // that and finish.
            Logger.debug("Worker found no jobs from request buffer.");
        }
    }
};




