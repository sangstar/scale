//
// Created by Sanger Steel on 5/23/25.
//

#include "benchmarks/dataset.hpp"

#include "logger.hpp"

#include "curl.hpp"
#include "benchmarks/constants.hpp"


bool Dataset::add_rows(std::string& uri) {
    auto resp = CURLHandler::get(uri.c_str());
    if (str_contains(resp, BenchmarkingConstants::rate_limit_text.data())) {
        Logger.info("Hit rate limit. Slowing down...");
        std::this_thread::sleep_for(std::chrono::milliseconds(30'000));
        return false;
    }
    json as_json;
    try {
        as_json = parse_to_json(resp);
    }
    catch (const json::parse_error& e) {
        return true;
    }
    rows.insert(rows.end(), as_json["rows"].begin(), as_json["rows"].end());
    return false;
}

std::unique_ptr<BenchmarkBase> create_benchmark(DatasetParams& params, Rows& rows) {
    const auto& config = params.config;
    if (config == "cola") {
        return std::make_unique<ColaBenchmark>(ColaBenchmark(std::move(rows)));
    }
    if (config == "mrpc") {
        return std::make_unique<MRPCBenchmark>(MRPCBenchmark(std::move(rows)));
    }
    throw std::runtime_error(std::format("No benchmark implementation for: {}", config));
}

std::string DatasetParams::get_url() {
    return std::format(BenchmarkingConstants::format_string,
                       id, config, split, offset);
}

Dataset DatasetParams::get_dataset() {
    Dataset dataset;
    bool is_finished = false;
    while (!is_finished) {
        auto url = get_url();
        offset+=rows_per_query;
        is_finished = dataset.add_rows(url);
        if (dataset.rows.size() >= max_rows) {
            is_finished = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_between_curl));
    }
    Logger.info(std::format("Got {} rows.", dataset.rows.size()));
    return std::move(dataset);
}
