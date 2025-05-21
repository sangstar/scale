//
// Created by Sanger Steel on 5/6/25.
//

#include "benchmark.hpp"
#include "../curl.hpp"


Logprobs::Logprobs(json logprobs_json) {
    logprobs_json.at("tokens").get_to(tokens);
    logprobs_json.at("token_logprobs").get_to(token_logprobs);
    auto top_logprobs_list = logprobs_json.at("top_logprobs");
    for (auto& top_logprob : top_logprobs_list) {
        auto tops = top_logprob.get<std::unordered_map<std::string, float>>();
        top_logprobs.emplace_back(tops);
    }

}

Choice::Choice(json choice_json) {
    choice_json.at("finish_reason").get_to(finish_reason);
    choice_json.at("text").get_to(text);
    choice_json.at("index").get_to(index);
    logprobs = Logprobs(choice_json.at("logprobs"));
}



CompletionResults::CompletionResults(std::string json_str) {
    auto as_json = json::parse(json_str);
    as_json.at("id").get_to(id);
    as_json.at("created").get_to(created);
    as_json.at("object").get_to(object);
    model = as_json.value("model", "N/A");
    auto choice_list = as_json.at("choices");
    for (auto& choice_json : choice_list) {
        auto choice = Choice(choice_json);
        choices.emplace_back(choice);
    }
}

json RequestParameters::to_json() {
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

