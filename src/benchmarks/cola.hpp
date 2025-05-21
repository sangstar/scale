//
// Created by Sanger Steel on 5/6/25.
//

#pragma once
#include "benchmark.hpp"

class ColaBenchmark {
public:
    ColaBenchmark(json&& data) : dataset(std::move(data)) {};
    ~ColaBenchmark() = default;
    RequestParameters request_from_dataset_row(int idx) const;
    json dataset;
    constexpr static std::string_view pre_formatted_text =
        "Is the following sentence grammatically acceptable?\n{}\nAnswer:";
};

