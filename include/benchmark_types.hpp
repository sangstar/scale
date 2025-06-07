//
// Created by Sanger Steel on 5/23/25.
//

#pragma once
#include <string>
#include <optional>
#include <iostream>

#include "constants.hpp"
#include "streaming_response.hpp"
#include "completion_types.hpp"
#include "curl.hpp"
#include "labels.hpp"
#include "result_types.hpp"

using RequestResultBuffer = std::shared_ptr<MPSCRingBuffer<RequestResult>>;
using CompletionResultsBuffer = std::shared_ptr<std::vector<CompletionResults>>;
using SharedClient = std::shared_ptr<CURLHandler>;

using Rows = std::vector<json>;

struct Data {
    Rows rows;

    bool add_rows(std::string& uri);
};

class DatasetParams {
public:
    const std::string id = "nyu-mll/glue";
    const std::string config = "cola";
    const std::string split = "train";
    int ms_between_curl = 500;
    int max_rows = 4000;

    DatasetParams(const char* id_, const char* config_, const char* split_)
        : id(std::string(id_)), config(std::string(config_)), split(std::string(split_)) {
    };

    int rows_per_query = 100;

    std::string get_url();

    int offset = 0;

    Data get_data();
};


struct Dataset {
    explicit Dataset(DatasetParams& params) {
        auto data_ = params.get_data();
        data = std::move(data_);
    }

    Data data;
    LabelStatesMapping map;
    std::string class_label_feature_name = "label";
    std::string pre_formatted_text;
    std::string_view prompt_feature_names_array[1] = {"sentence"};
};


class DatasetToRequestStrategy {
public:
    explicit DatasetToRequestStrategy(Dataset dataset) : dataset(std::move(dataset)) {
    };

    Dataset& get_dataset() {
        return dataset;
    }

    virtual ~DatasetToRequestStrategy() = default;

    virtual size_t dataset_size();

    virtual std::string get_prompt_from_row(json& row);

    virtual RequestParameters fill_req_from_row(const Dataset& dataset, int row_idx, RequestParameters& req);

private:
    Dataset dataset;
};

struct RequestProcessingParameters {
    std::shared_ptr<StreamingResponse> resp;
    CompletionResultsBuffer compl_result_buffer;
    int max_retries;
};

class RequestTransportStrategy {
public:
    virtual ~RequestTransportStrategy() = default;

    RequestResultBuffer request_results_buffer = std::make_shared<MPSCRingBuffer<RequestResult>>();

    virtual void send_and_add_to_buffer(
        const Dataset& bench,
        RequestParameters& req,
        SharedClient& shared_client
    );

    int fetch_and_add_job_id() {
        return job_id.fetch_add(1, std::memory_order_acquire);
    }

private:
    std::atomic<int> job_id;
};

class FileWritingStrategy {
public:
    virtual ~FileWritingStrategy() = default;

    virtual void write_to_jsonl_from_results_buffer(
        Metrics& metrics,
        RequestResultBuffer& buf,
        const Dataset& dataset
    );

    void finalize() {
        can_finish = true;
    }

private:
    bool can_finish = false;
};

struct ProcessingStrategy {
    DatasetToRequestStrategy& dataset_processor;
    RequestTransportStrategy& sender_and_parser;
    FileWritingStrategy& writer;
    SharedClient shared_client;

    FinalMetrics process_benchmark(
        const char* filename_jsonl
    );
};

LabelStates get_label_state(const Dataset& dataset, const RequestParameters& req);

void get_request_and_send_loop(
    const Dataset& benchmark,
    RequestTransportStrategy& sender_and_parser,
    DatasetToRequestStrategy& data_processor,
    SharedClient shared_client
);
