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
#include "result_types.hpp"
#include "yaml-cpp/yaml.h"
#include "logger.hpp"

using RequestResultBuffer = std::shared_ptr<MPSCRingBuffer<RequestResult>>;
using CompletionResultsBuffer = std::shared_ptr<std::vector<CompletionResults>>;
using SharedClient = std::shared_ptr<CURLHandler>;

using Rows = std::vector<json>;

struct Data {
    Rows rows;
};


struct Config {
    struct Value {
        std::string response;
        int id;
    };
    struct Dataset {
        std::string tag;
        std::string subset;
        std::string split;
    };
    std::string pre_formatted_prompt;
    std::vector<std::string> sentence_tags;
    struct ClassLabel {
        std::string tag;
        std::vector<Value> values;
    };
    Dataset dataset;
    ClassLabel label;
};

class DatasetParsingStrategy {
public:

    virtual ~DatasetParsingStrategy() = default;

    virtual std::string get_url() = 0;

    virtual Data& get_data();

    virtual void download() = 0;

    virtual bool add_rows(Data& data, std::string& uri) = 0;

    virtual Config& get_config();

    virtual json& get_row(int row_idx) = 0;

    int max_rows = 10000;

protected:
    int ms_between_curl = 500;
    int offset = 0;
    int rows_per_query = 100;
    Config cfg;
    Data data;
};

using Dataset = std::unique_ptr<DatasetParsingStrategy>;

class HFDatasetParser final : public DatasetParsingStrategy {
public:

    HFDatasetParser(const char* yaml_filename) :
          config_yaml(YAML::LoadFile(yaml_filename)) {
        initialize_config();
    }


    void initialize_config();

    std::string get_url() override;

    void download() override;

    bool add_rows(Data& data, std::string& uri) override;

    void parse_rows(Data& data, json& rows);

    json& get_row(int row_idx) override;



private:
    YAML::Node config_yaml;
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

    virtual void fill_req_from_row(const Dataset& dataset, int row_idx, RequestParameters& req);

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
        const Dataset& dataset,
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

void get_request_and_send_loop(
    const Dataset& benchmark,
    RequestTransportStrategy& sender_and_parser,
    DatasetToRequestStrategy& data_processor,
    SharedClient shared_client
);
