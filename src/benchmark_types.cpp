//
// Created by Sanger Steel on 6/6/25.
//

#include "benchmark_types.hpp"

#include <fstream>

#include "utils.hpp"

std::string debug_json(json& j) {
    return j.dump(2);
}

std::string DatasetParams::get_url() {
    return std::format(BenchmarkingConstants::format_string,
                       id, config, split, offset);
}

Data DatasetParams::get_data() {
    Data data;
    bool is_finished = false;
    while (!is_finished) {
        auto url = get_url();
        offset += rows_per_query;
        is_finished = data.add_rows(url);
        if (data.rows.size() >= max_rows) {
            is_finished = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_between_curl));
    }
    Logger.info(std::format("Got {} rows.", data.rows.size()));
    return std::move(data);
}

LabelStates get_label_state(const Dataset& dataset, const RequestParameters& req) {
    auto label = req.golden_label;
    for (auto [k, v]: dataset.map) {
        if (label == k) {
            return v;
        }
    }
    return NO_LABEL;
}

void get_request_and_send_loop(
    const Dataset& dataset,
    RequestTransportStrategy& sender_and_parser,
    DatasetToRequestStrategy& data_processor,
    std::shared_ptr<CURLHandler> shared_client
) {
    RequestParameters req;
    while (true) {
        auto idx = sender_and_parser.fetch_and_add_job_id();

        if (idx >= data_processor.dataset_size()) {
            break;
        }

        data_processor.fill_req_from_row(dataset, idx, req);
        sender_and_parser.send_and_add_to_buffer(dataset, req, shared_client);
    }
};

bool Data::add_rows(std::string& uri) {
    auto resp = CURLHandler::get(uri.c_str());
    if (str_contains(resp, BenchmarkingConstants::rate_limit_text.data())) {
        Logger.info("Hit rate limit. Slowing down...");
        std::this_thread::sleep_for(std::chrono::milliseconds(30'000));
        return false;
    }
    json as_json;
    try {
        as_json = parse_to_json(resp);
    } catch (const json::parse_error& e) {
        Logger.debug("Got parser error from resp: {}", resp);
        return true;
    }
    auto& rows_feature = as_json["rows"];
    if (!rows_feature.is_array()) {
        Logger.error(std::format("Expected 'rows' to be an array, got: {}", rows_feature.dump(2)));
        return false;
    }
    rows.insert(rows.end(), as_json["rows"].begin(), as_json["rows"].end());
    return false;
}

size_t DatasetToRequestStrategy::dataset_size() {
    return this->dataset.data.rows.size();
}

std::string DatasetToRequestStrategy::get_prompt_from_row(json& row) {
    auto& sentence_str = this->dataset.prompt_feature_names_array[0];
    Logger.debug("Row: {}", debug_json(row));
    auto substituted = row[sentence_str].get<std::string>();
    Logger.debug("Got prompt from row: {}", substituted);
    return std::vformat(dataset.pre_formatted_text, std::make_format_args(substituted));
}

RequestParameters DatasetToRequestStrategy::fill_req_from_row(
    const Dataset& dataset, int row_idx,
    RequestParameters& req
) {
    auto row = dataset.data.rows[row_idx]["row"];
    req.golden_label = row[dataset.class_label_feature_name].dump();
    req.prompt = this->get_prompt_from_row(row);
    return req;
}

void fetch_response_and_add_to_results_buffer(
    const RequestProcessingParameters& params,
    const std::shared_ptr<CURLHandler>& shared_client
) {
    std::mutex compl_buffer_mutex;
    int retries = 0;
    std::string* json_str;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(params.resp->mu);
            params.resp->cv.wait(lock, [&params] {
                return params.resp->ready_to_fetch();
            });
        }
        bool producer_finished = shared_client->write_to_buffer_finished(params.resp);
        auto state = shared_client->fetch(params.resp, json_str);
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
                std::lock_guard<std::mutex> lock(compl_buffer_mutex);
                params.compl_result_buffer->emplace_back(std::move(results));
            }
        } else if (state == EMPTY && producer_finished) {
            if (params.max_retries == retries) {
                break;
            }
            retries++;
        }
    }
}


void RequestTransportStrategy::send_and_add_to_buffer(
    const Dataset& bench,
    RequestParameters& req,
    std::shared_ptr<CURLHandler>& shared_client
) {
    RequestProcessingParameters params{
        .resp = shared_client->post_stream(req),
        .max_retries = 2,
        .compl_result_buffer = std::make_shared<std::vector<CompletionResults>>()
    };


    // Deploy workers to grab responses from buffer, process them to CompletionResults types,
    // and add them to res.
    std::vector<std::thread> workers;
    for (int i = 0; i < WorkerConstants::NumWorkersPerRequest; ++i) {
        std::thread t([this, &params, shared_client]() {
            fetch_response_and_add_to_results_buffer(params, shared_client);
        });
        workers.emplace_back(std::move(t));
    }

    // Await the request's completion
    auto latencies = shared_client->await(params.resp);

    // Wait for the workers deployed to finish
    for (int i = 0; i < WorkerConstants::NumWorkersPerRequest; ++i) {
        workers[i].join();
    }

    // Process the buffer `res`
    if (!params.compl_result_buffer->empty()) {
        RequestResult result;

        // res doesn't need to be shared anymore since no one is reading
        // or writing to it (no more surprises), so dereference off
        // the shared_ptr safeguard and move the result for the caller
        // to own it.
        result.completion_results = std::move(*params.compl_result_buffer);
        result.latencies = latencies;
        result.params = req;
        auto state = get_label_state(bench, result.params);
        result.guessed_correctly = guessed_correctly(state, result);
        request_results_buffer->push(result);
    } else {
        // If the worker found no work to be processed from the stream buffer, just make a note of
        // that and finish.
        Logger.debug("Worker found no jobs from request buffer.");
    }
}

void FileWritingStrategy::write_to_jsonl_from_results_buffer(
    Metrics& metrics,
    RequestResultBuffer& buf,
    const Dataset& dataset
) {
    std::ofstream outfile(metrics.output_jsonl);

    if (!outfile.is_open()) {
        throw std::runtime_error("failed to open output file");
    }


    // With many producers, it's unlikely for this thread to be so quick
    // that it finishes early due to an empty buffer before all producers
    // are finished, so we can probably get away with not signaling done
    RequestResult* result;
    while (true) {
        auto state = buf->fetch(result);
        if (state == SUCCESS) {
            metrics.req_results.emplace_back(*result);
            metrics.requests_processed++;
            auto jsons = get_output_json(*result, dataset);
            for (auto& jsonl: jsons) {
                auto jsonl_str = jsonl.dump();
                Logger.info(std::format("Req {}: {}", metrics.requests_processed, jsonl.dump()));
                outfile << jsonl_str << '\n';
            }
        }
        if (state == EMPTY && this->can_finish) {
            break;
        }
    }
    metrics.benchmark_end = std::chrono::high_resolution_clock::now();
    outfile.close();
}


FinalMetrics ProcessingStrategy::process_benchmark(const char* filename_jsonl) {
    Metrics metrics = Metrics(filename_jsonl);

    std::thread writer_thread([this, &metrics]() {
        this->writer.write_to_jsonl_from_results_buffer(
            metrics,
            this->sender_and_parser.request_results_buffer,
            this->dataset_processor.get_dataset()
        );
    });

    std::vector<std::thread> workers;
    for (int i = 0; i < WorkerConstants::NumConcurrentRequests; ++i) {
        std::thread t([this]() {
            get_request_and_send_loop(
                this->dataset_processor.get_dataset(),
                this->sender_and_parser,
                this->dataset_processor,
                this->shared_client
            );
        });
        workers.emplace_back(std::move(t));
    }
    for (int i = 0; i < WorkerConstants::NumConcurrentRequests; ++i) {
        workers[i].join();
    }

    this->writer.finalize();
    writer_thread.join();
    auto final_metrics = get_results(metrics);
    return final_metrics;
}
