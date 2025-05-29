//
// Created by Sanger Steel on 5/28/25.
//

#pragma once
#include "../benchmark.hpp"

class MRPCBenchmark : public BenchmarkBase {
public:
    constexpr static std::pair<std::string_view, LabelStates> label_map[] = {
        {"-1", NO_LABEL},
        {"0", NO},
        {"1", YES},
    };

    constexpr static std::string_view pre_formatted_text =
            "Is this pair of sentences here semantically equivalent?\nSentence 1: {}\nSentence 2: {}\nAnswer:";

    constexpr static std::string_view class_label_feature_name = "label";

    constexpr static std::string_view prompt_feature_names_array[2] = {"sentence1", "sentence2"};
    constexpr static const std::string_view* prompt_feature_names = prompt_feature_names_array;


    std::string get_prompt(json& row);
};

inline std::string MRPCBenchmark::get_prompt(json& row) {
    auto& sentence_1 = prompt_feature_names[0];
    auto& sentence_2 = prompt_feature_names[1];
    auto substituted_1 = row[sentence_1];
    auto substituted_2 = row[sentence_2];
    return std::format(pre_formatted_text, static_cast<std::string>(substituted_1), static_cast<std::string>(substituted_1));
}




