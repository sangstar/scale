//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <string>
#include "../external/json.hpp"


using json = nlohmann::json;
using TopLogprobs = std::vector<std::unordered_map<std::string, float>>;

struct logprob_entry {
    std::string text;
    std::optional<float> logprob = std::nullopt;
    std::string dump() {
        if (logprob.has_value()) {
            return std::format("{}",logprob.value());
        }
        return std::format("N/A", text);
    }
};

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
    CompletionResults(std::string json_str);
    std::string id;
    std::string object;
    int created;
    std::vector<Choice> choices;
    std::string model;
};
