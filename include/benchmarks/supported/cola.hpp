//
// Created by Sanger Steel on 5/6/25.
//

#pragma once
#include "../benchmark.hpp"

class ColaBenchmark : public BenchmarkBase {
public:

    constexpr static std::pair<std::string_view, LabelStates> label_map[] = {
        {"-1", NO_LABEL},
        {"0", NO},
        {"1", YES},
    };

    constexpr static std::string_view pre_formatted_text =
            "Is the following sentence grammatically acceptable?\n{}\nAnswer:";

    constexpr static std::string_view class_label_feature_name = "label";

    constexpr static std::string_view prompt_feature_names_array[1] = {"sentence"};
    constexpr static const std::string_view* prompt_feature_names = prompt_feature_names_array;


    std::string get_prompt(json& row);
};

inline std::string ColaBenchmark::get_prompt(json& row) {
    auto& sentence_str = prompt_feature_names[0];
    auto substituted = row[sentence_str];
    return std::format(pre_formatted_text, static_cast<std::string>(substituted));
}




