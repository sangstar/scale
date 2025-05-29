//
// Created by Sanger Steel on 5/23/25.
//

#pragma once
#include <string>
#include <vector>
#include "../../external/json.hpp"
#include "benchmark.hpp"
#include "supported/cola.hpp"
#include "supported/mrpc.hpp"

using json = nlohmann::json;

struct Dataset;

class DatasetParams {
public:
    const std::string id = "nyu-mll/glue";
    const std::string config = "cola";
    const std::string split = "train";
    int ms_between_curl = 500;
    int max_rows = 1000;

    DatasetParams(const char* id_, const char* config_, const char* split_)
        : id(std::string(id_)), config(std::string(config_)), split(std::string(split_)) {};

    int rows_per_query = 100;

    std::string get_url();

    // TODO: Have some struct that represents the results of each
    //       100 rows here, and then be able to seamlessly combine them
    //       refactor the benchmark concept stuff to account for this, and COLA
    Dataset get_dataset();

private:
    int offset = 0;
};

struct Dataset {
    Dataset() = default;
    Rows rows;
    bool add_rows(std::string& uri);
};

std::unique_ptr<BenchmarkBase> create_benchmark(DatasetParams& params, Rows& rows);
