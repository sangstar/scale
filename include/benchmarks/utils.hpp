//
// Created by Sanger Steel on 5/23/25.
//

#pragma once
#include "labels.hpp"
#include "../result_types.hpp"

struct logprob_entry {
    std::string text;
    std::optional<float> logprob = std::nullopt;

    std::string dump();
};


struct YesNoLogprobPair {
    std::optional<logprob_entry> yes = std::nullopt;
    std::optional<logprob_entry> no = std::nullopt;
};

bool guessed_correctly(LabelStates state, const RequestResult& res);
void report_results(FinalMetrics& metrics);
YesNoLogprobPair get_yes_no_logprobs(LabelStates state, bool correct, const RequestResult& res);
