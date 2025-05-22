//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <string>
#include "../external/json.hpp"

using json = nlohmann::json;

struct RequestParameters {
    std::string model = "gpt-3.5-turbo-instruct";
    bool echo = true;
    std::string prompt;
    float temperature = 1;
    int num_logprobs = 100;
    int max_tokens = 1;
    int top_k = 1;
    bool stream = true;
    std::string golden_label;
    json to_json();
};

