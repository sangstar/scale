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
    int golden_label;

    json to_json();
};

inline json RequestParameters::to_json() {
    json j;
    j["model"] = model;
    j["prompt"] = prompt;
    // j["echo"] = echo;
    j["max_tokens"] = max_tokens;
    j["logprobs"] = num_logprobs;
    j["temperature"] = temperature;
    // j["top_k"] = top_k;
    j["stream"] = stream;
    return j;
}

