//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <string>
#include "../external/json.hpp"
#include <format>

using json = nlohmann::json;

// TODO: Customizing this is not currently exposed
//       these defaults won't do for non-QA tasks
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
    std::string to_str();

};

inline json RequestParameters::to_json() {
    json j;
    j["model"] = model;
    j["prompt"] = prompt;
    j["echo"] = echo;
    j["max_tokens"] = max_tokens;
    j["logprobs"] = num_logprobs;
    j["temperature"] = temperature;
    if (top_k != -1) {
        j["top_k"] = top_k;
    }
    j["stream"] = stream;
    return j;
}

inline std::string RequestParameters::to_str() {
    std::string str = "==========\nREQUEST PARAMETERS\n";
    str += std::format("model: {}\n", model);
    str += std::format("echo: {}\n", echo);
    str += std::format("prompt: {}\n", prompt);
    str += std::format("temperature: {}\n", temperature);
    str += std::format("num_logprobs: {}\n", num_logprobs);
    str += std::format("max_tokens: {}\n", max_tokens);
    str += std::format("top_k: {}\n", top_k);
    str += std::format("stream: {}\n", stream);
    str += "==========\n";
    return str;
}

