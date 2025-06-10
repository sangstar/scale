//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <string>
#include "../external/json.hpp"


using json = nlohmann::json;
using TopLogprobs = std::vector<std::unordered_map<std::string, float>>;

TopLogprobs sort_top_logprobs(TopLogprobs& tops);


struct Logprobs {
    Logprobs() = default;

    Logprobs(json logprobs_json);

    ~Logprobs() = default;

    std::vector<std::string> tokens;
    std::vector<float> token_logprobs;
    TopLogprobs top_logprobs;
};

struct Choice {
    Logprobs logprobs;
    std::string finish_reason;
    int index;
    std::string text;

    Choice(json choice_json);
};

// TODO: I've allowed a vector of Choices here,
//       even though I've only seen responses with just
//       1 Choice per streamed response, and the workflow
//       here reflects that assumption
struct CompletionResults {
    CompletionResults() = default;

    ~CompletionResults() = default;

    explicit CompletionResults(std::string json_str);

    std::string id;
    std::string object;
    int created;
    std::vector<Choice> choices;
    std::string model;

    std::string to_string() {
        std::string str;
        str += id + ", ";
        for (auto& choice: choices) {
            str += choice.index + ", ";
            str += choice.text + ", ";
        }
        return std::move(str);
    }
};
