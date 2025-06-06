//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <string>
#include "../external/json.hpp"


using json = nlohmann::json;
using TopLogprobs = std::vector<std::unordered_map<std::string, float>>;


struct Logprobs {
    Logprobs() = default;

    Logprobs(json logprobs_json) {
        logprobs_json.at("tokens").get_to(tokens);
        logprobs_json.at("token_logprobs").get_to(token_logprobs);
        auto top_logprobs_list = logprobs_json.at("top_logprobs");
        for (auto& top_logprob: top_logprobs_list) {
            auto tops = top_logprob.get<std::unordered_map<std::string, float>>();
            top_logprobs.emplace_back(tops);
        }
    }

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

    Choice(json choice_json) {
        choice_json.at("finish_reason").get_to(finish_reason);
        choice_json.at("text").get_to(text);
        choice_json.at("index").get_to(index);
        logprobs = Logprobs(choice_json.at("logprobs"));
    }
};

// TODO: I've allowed a vector of Choices here,
//       even though I've only seen responses with just
//       1 Choice per streamed response, and the workflow
//       here reflects that assumption
struct CompletionResults {
    CompletionResults() = default;

    ~CompletionResults() = default;

    explicit CompletionResults(std::string json_str) {
        auto as_json = json::parse(json_str);
        as_json.at("id").get_to(id);
        as_json.at("created").get_to(created);
        as_json.at("object").get_to(object);
        model = as_json.value("model", "N/A");
        auto choice_list = as_json.at("choices");
        for (auto& choice_json: choice_list) {
            auto choice = Choice(choice_json);
            choices.emplace_back(choice);
        }
    }

    std::string id;
    std::string object;
    int created;
    std::vector<Choice> choices;
    std::string model;
};
