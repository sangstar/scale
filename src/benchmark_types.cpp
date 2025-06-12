//
// Created by Sanger Steel on 6/6/25.
//

#include "benchmark_types.hpp"

#include <fstream>

#include "utils.hpp"
#include <stdexcept>


std::string debug_json(json& j) {
    return j.dump(2);
}


#define TODO() throw std::logic_error("TODO hit at " __FILE__ ":" + std::to_string(__LINE__))


Data& DatasetParsingStrategy::get_data() {
    return data;
}

Config& DatasetParsingStrategy::get_config() {
    return this->cfg;
}

void HFDatasetParser::initialize_config() {
    Config config;
    Config::Dataset dataset;
    Config::ClassLabel label;
    config.pre_formatted_prompt = config_yaml["pre_formatted_prompt"].as<std::string>();
    config.sentence_tags = config_yaml["sentence_tags"].as<std::vector<std::string>>();

    dataset.tag = config_yaml["dataset"]["tag"].as<std::string>();
    dataset.subset = config_yaml["dataset"]["subset"].as<std::string>();
    dataset.split = config_yaml["dataset"]["split"].as<std::string>();
    config.dataset = dataset;

    label.tag = config_yaml["class_label"]["tag"].as<std::string>();

    std::vector<Config::Value> value_vec;
    for (const auto& value: config_yaml["class_label"]["values"]) {
        Config::Value val;
        val.id = value["id"].as<int>();
        val.response = value["response"].as<std::string>();
        value_vec.emplace_back(std::move(val));
    }
    label.values = std::move(value_vec);

    config.label = label;
    cfg = std::move(config);
}

std::string HFDatasetParser::get_url() {
    return std::format(BenchmarkingConstants::format_string,
                       cfg.dataset.tag, cfg.dataset.subset, cfg.dataset.split, this->offset);
}

void HFDatasetParser::download() {
    int failed_requests = 0;
    int max_failed_requests = 10;

    while (true) {
        auto url = get_url();
        offset += rows_per_query;
        if (!add_rows(data, url)) {
            failed_requests++;
            if (failed_requests >= max_failed_requests) {
                throw std::runtime_error("Failed parsing dataset");
            }
            Logger.info("Got bad response when downloading dataset. Will try again in a few moments..");
            std::this_thread::sleep_for(std::chrono::milliseconds(10'000));
        };
        if (data.rows.size() >= max_rows) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_between_curl));
    }
    Logger.info(std::format("Got {} rows.", data.rows.size()));
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
        Logger.num_requests_sent.fetch_add(1, std::memory_order_acq_rel);
    }
};

bool HFDatasetParser::add_rows(Data& data, std::string& uri) {
    auto resp = CURLHandler::get(uri.c_str());
    if (str_contains(resp, BenchmarkingConstants::rate_limit_text.data())) {
        Logger.info("Hit rate limit. Slowing down...");
        std::this_thread::sleep_for(std::chrono::milliseconds(30'000));
        return true;
    }
    json as_json;
    try {
        as_json = parse_to_json(resp);
    } catch (const json::parse_error& e) {
        Logger.debug("Got parser error from resp: {}", resp);
        return false;
    }
    parse_rows(data, as_json["rows"]);
    return true;
}

void HFDatasetParser::parse_rows(Data& data, json& rows) {
    if (!rows.is_array()) {
        throw std::runtime_error(std::format("Expected 'rows' to be an array, got: {}", rows.dump(2)));
    }
    if (rows.empty()) {
        throw std::runtime_error(std::format("Rows is empty: {}", rows.dump(2)));
    }
    data.rows.insert(data.rows.end(), rows.begin(), rows.end());
}

json& HFDatasetParser::get_row(int row_idx) {
    return data.rows[row_idx]["row"];
}

size_t DatasetToRequestStrategy::dataset_size() {
    return this->dataset->get_data().rows.size();
}

std::string DatasetToRequestStrategy::get_prompt_from_row(json& row) {
    const auto& cfg = dataset->get_config();
    std::vector<const std::string> sentences;
    std::vector<const std::string> possible_answers;
    for (const auto& sentence_tag: cfg.sentence_tags) {
        sentences.emplace_back(row[sentence_tag]);
    }
    for (const auto& value: cfg.label.values) {
        possible_answers.emplace_back(value.response);
    }
    auto pre_formatted_task = std::vformat(cfg.pre_formatted_prompt, std::make_format_args(join(sentences, " | ")));

    auto prompt = std::vformat("{}\nPlease choose from the following choices: {}\n Answer: ",
                               std::make_format_args(pre_formatted_task, join(possible_answers)));
    return std::move(prompt);
}

void DatasetToRequestStrategy::fill_req_from_row(
    const Dataset& dataset, int row_idx,
    RequestParameters& req
) {
    const auto& cfg = dataset->get_config();

    auto row = dataset->get_row(row_idx);

    req.golden_label = row[cfg.label.tag];
    req.prompt = this->get_prompt_from_row(row);
}

void fetch_response_and_add_to_results_buffer(
    const RequestProcessingParameters& params,
    const std::shared_ptr<CURLHandler>& shared_client
) {
    std::mutex compl_buffer_mutex;
    int consecutive_retries = 0;
    std::string json_str;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(params.resp->mu);
            params.resp->cv.wait(lock, [&params] {
                return params.resp->ready_to_fetch();
            });
        }
        auto fetched_result = shared_client->fetch(params.resp);
        Logger.fetch_attempts.fetch_add(1, std::memory_order_acq_rel);
        if (fetched_result.state == RingState::SUCCESS) {
            consecutive_retries = 0;
            Logger.fetched_requests.fetch_add(1, std::memory_order_acq_rel);

            json_str = fetched_result.content.value();
            if (json_str.empty()) {
                throw std::runtime_error("Fetched json string is empty");
            }
            CompletionResults results(std::move(json_str));

            // For finish_reason = length, an empty response
            // is thrown back at the end indicating it reached
            // the finish_reason. Don't include these results.
            // TODO: The logic here is likely flimsy and it assumes
            //       choices has only one element (which is all I'm
            //       used to seeing and assuming). What if there are
            //       multiple Choices and one has text = "" and one doesn't?
            //       this would disqualify all

            bool fine_to_add = false;
            for (auto& choice: results.choices) {
                if (choice.text != "") {
                    fine_to_add = true;
                }
            }

            // TODO: Make this atomic
            if (fine_to_add) {
                std::lock_guard<std::mutex> lock(compl_buffer_mutex);
                if (results.to_string().empty()) {
                    throw std::runtime_error("Result data was invalidated.");
                }
                Logger.requests_sent_to_compl_buffer.fetch_add(1, std::memory_order_acq_rel);
                params.compl_result_buffer->emplace_back(std::move(results));
            } else {
                Logger.disallowed_requests.fetch_add(1, std::memory_order_acq_rel);
            }

        } else if (fetched_result.state == RingState::EMPTY && params.finished) {
            if (consecutive_retries >= params.max_retries) {
                break;
            }
            consecutive_retries++;
        }
    }
}


void RequestTransportStrategy::send_and_add_to_buffer(
    const Dataset& dataset,
    RequestParameters& req,
    std::shared_ptr<CURLHandler>& shared_client
) {
    RequestProcessingParameters params{
        .resp = shared_client->post_stream(req),
        .max_retries = 100,
        .compl_result_buffer = std::make_shared<std::vector<CompletionResults>>()
    };

    Logger.send_add_to_buffer_calls.fetch_add(1, std::memory_order_acq_rel);
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
    params.finished.store(true, std::memory_order_release);

    // Wait for the workers deployed to finish
    for (int i = 0; i < WorkerConstants::NumWorkersPerRequest; ++i) {
        workers[i].join();
    }

    params.resp->feedback.evaluate();

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
        result.guessed_correctly = guessed_correctly(dataset, result);
        request_results_buffer->push(std::move(result));
        Logger.num_processed.fetch_add(1, std::memory_order_acq_rel);
    } else {
        // If the worker found no work to be processed from the stream buffer, just make a note of
        // that and finish.
        Logger.debug("Worker found no jobs from request buffer.");
        Logger.failed_send_and_add_to_buffer_calls.fetch_add(1, std::memory_order_acq_rel);

    }
}

// TODO: Processing less requests than asked for.
//       Have some lifecycle tracking for each request to
//       see if any slipped through the cracks
void FileWritingStrategy::write_to_jsonl_from_results_buffer(
    Metrics& metrics,
    RequestResultBuffer& buf,
    const Dataset& dataset
) {
    std::ofstream outfile(metrics.output_jsonl);

    if (!outfile.is_open()) {
        throw std::runtime_error("failed to open output file");
    }

    int max_consecutive_retries = 100;
    int consecutive_retries = 0;

    RequestResult result;
    while (true) {
        auto fetch_attempt = buf->fetch();
        if (fetch_attempt.state == RingState::SUCCESS) {
            consecutive_retries = 0;

            result = fetch_attempt.content.value();
            metrics.req_results.emplace_back(result);
            metrics.requests_processed++;
            auto jsons = get_output_json(result, dataset);

            for (auto& jsonl: jsons) {
                auto jsonl_str = jsonl.dump();
                Logger.info(std::format("Req {}: {}", metrics.requests_processed, jsonl.dump()));
                outfile << jsonl_str << '\n';
            }
        } else {
            if (fetch_attempt.state == RingState::EMPTY && this->can_finish) {
                if (consecutive_retries >= max_consecutive_retries) {
                    break;
                }
                Logger.debug("Writer retrying read. Consecutive retries: {}\n", std::to_string(consecutive_retries));
                consecutive_retries++;
            }
            std::this_thread::yield();
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
