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
    size_t size();

    static constexpr std::pair<std::string_view, LabelStates> label_map[] = {
        {"-1", NO_LABEL},
        {"0", NO},
        {"1", YES},
    };

    constexpr static std::string_view pre_formatted_text =
        "Is the following sentence grammatically acceptable?\n{}\nAnswer:";
};

