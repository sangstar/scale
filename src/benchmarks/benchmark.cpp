//
// Created by Sanger Steel on 5/6/25.
//

#include "benchmark.hpp"
#include "../curl.hpp"

// These trimming functions are from:
// https://stackoverflow.com/a/25385766/8825740
const char* ws = " \t\n\r\f\v";

inline std::string& rtrim(std::string& s, const char* t = ws)
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

inline std::string& ltrim(std::string& s, const char* t = ws)
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}

inline std::string& trim(std::string& s, const char* t = ws)
{
    return ltrim(rtrim(s, t), t);
}

inline void lower(std::string& to_lower) {
    for (auto &c: to_lower) {
        c = std::tolower(c);
    }
}

inline std::string parse_guessed_string(std::string& guess) {
    auto trimmed = trim(guess);
    lower(trimmed);
    return std::move(trimmed);
}

bool guess_grade(const Label label, const Results& res) {
    auto crucial_result = res.completion_results[0];
    // Only allow the first choice
    auto crucial_choice = crucial_result.choices[0];
    auto guess = crucial_choice.text;
    auto parsed = parse_guessed_string(guess);
    for (const auto& viable : label.allowed_strings) {
        if (viable == parsed) {
            return true;
        }
    }
    return false;
}


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

// TODO: Will eventually want to consider logprobs for answers
//       to calculate scores for probabilities for stuff like F1
//       down the line rather than only getting right/wrong tallied
//       For instance, if answer to a question was given "Yes", but "No"
//       was in the top logprobs, it would be useful to get the probs
//       for both down the line
bool guessed_correctly(LabelStates state, const Results& res) {
    switch (state) {
        case TRUE:
            return guess_grade(YesLabel, res);
        case FALSE:
            return guess_grade(NoLabel, res);
        default:
            return false;
    }
}

void report_results(FinalMetrics& metrics) {
    std::function<void(std::string)> polymorphic_writer;
        if (metrics.logger) {
            polymorphic_writer = [=](std::string to_write) {
                metrics.logger->write(to_write.c_str());
            };
        }
        else {
            polymorphic_writer = [](std::string to_write) {
                std::cout << to_write << std::endl;
            };
        }

    auto benchmark_duration = metrics.benchmark_end - metrics.benchmark_start;
    auto seconds = duration_cast<std::chrono::duration<double>>(benchmark_duration).count();
    polymorphic_writer(std::format("{} requests received in {}s.", metrics.requests_processed, seconds));
}
