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

bool guess_grade(const Label label, const RequestResults& res) {
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
bool guessed_correctly(LabelStates state, const RequestResults& res) {
    switch (state) {
        case YES:
            return guess_grade(YesLabel, res);
        case NO:
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
    polymorphic_writer(std::format("{} requests processed in {}s.", metrics.requests_processed, seconds));
}

inline std::optional<float> get_logprob(std::string key, float value, const Label& label) {
    auto parsed_token = parse_guessed_string(key);
    for (const auto& allowed_str : label.allowed_strings) {
        if (parsed_token == allowed_str) {
            return value;
        }
    }
    return std::nullopt;
}


// TODO: This `crucial_result`, `crucial_choice` stuff needs a refactor
//       where the access of which result or choice to use is determined by some
//       function to make this less rigid
YesNoLogprobPair get_yes_no_logprobs(LabelStates state, bool correct, const RequestResults& res) {
    YesNoLogprobPair pair;
    auto logprobs = res.completion_results[0].choices[0].logprobs;
    if (correct) {
        auto correct_guess_logprob = res.completion_results[0].choices[0].logprobs.token_logprobs[0];
        auto correct_guess_logprob_text = res.completion_results[0].choices[0].logprobs.tokens[0];
        switch (state) {
            case YES:
                pair.yes = logprob_entry{.logprob = correct_guess_logprob, .text = correct_guess_logprob_text};
                break;
            case NO:
                pair.no = logprob_entry{.logprob = correct_guess_logprob, .text = correct_guess_logprob_text};
                break;
            default:
                break;
        }
    }
    // TODO: Need to order the TopLogprobs in descending order, otherwise I risk saying the logprob for
    //       "No" is based off a TopLogprob " NO" with logprob -15.242 when there was a "no" logprob with -2,
    //       which would deflate the performance eval here.
    for (const auto& top_logprob: logprobs.top_logprobs) {
        for (const auto& [k,v] : top_logprob) {
            std::string key = k;
            if (!pair.yes.has_value()) {
                auto maybe_yes_logprob = get_logprob(key, v, YesLabel);
                if (maybe_yes_logprob.has_value()) {
                    pair.yes = logprob_entry{key, maybe_yes_logprob.value()};
                }
            } else if (!pair.no.has_value()) {
                auto maybe_no_logprob = get_logprob(key, v, NoLabel);
                if (maybe_no_logprob.has_value()) {
                    pair.no = logprob_entry{key, maybe_no_logprob.value()};
                }
            }
        }
    }
    return std::move(pair);
}
