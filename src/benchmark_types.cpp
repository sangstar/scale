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
    bool is_finished = false;
    while (!is_finished) {
        auto url = get_url();
        offset += rows_per_query;
        is_finished = add_rows(data, url);
        if (data.rows.size() >= max_rows) {
            is_finished = true;
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
    }
};

bool HFDatasetParser::add_rows(Data& data, std::string& uri) {
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
    parse_rows(data, as_json["rows"]);
    return false;
}

void HFDatasetParser::parse_rows(Data& data, json& rows) {
    if (!rows.is_array()) {
        Logger.error(std::format("Expected 'rows' to be an array, got: {}", rows.dump(2)));
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
    for (const auto& sentence_tag : cfg.sentence_tags) {
        sentences.emplace_back(row[sentence_tag]);
    }
    for (const auto& value : cfg.label.values) {
        possible_answers.emplace_back(value.response);
    }
    auto pre_formatted_task = std::vformat(cfg.pre_formatted_prompt, std::make_format_args(join(sentences, "| ")));

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
    const Dataset& dataset,
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
        result.guessed_correctly = guessed_correctly(dataset, result);
        request_results_buffer->push(std::move(result));
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
