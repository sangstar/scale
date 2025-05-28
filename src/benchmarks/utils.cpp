//
// Created by Sanger Steel on 5/23/25.
//

#include "benchmarks/utils.hpp"
#include <iostream>


std::string logprob_entry::dump() {
    if (logprob.has_value()) {
        return std::format("{}", logprob.value());
    }
    return std::format("N/A", text);
}

// These trimming functions are from:
// https://stackoverflow.com/a/25385766/8825740
const char* ws = " \t\n\r\f\v";

inline std::string& rtrim(std::string& s, const char* t = ws) {
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

inline std::string& ltrim(std::string& s, const char* t = ws) {
    s.erase(0, s.find_first_not_of(t));
    return s;
}

inline std::string& trim(std::string& s, const char* t = ws) {
    return ltrim(rtrim(s, t), t);
}

inline void lower(std::string& to_lower) {
    for (auto& c: to_lower) {
        c = std::tolower(c);
    }
}

inline std::string parse_guessed_string(std::string& guess) {
    auto trimmed = trim(guess);
    lower(trimmed);
    return std::move(trimmed);
}

bool guess_grade(const Label label, const RequestResult& res) {
    auto crucial_result = res.completion_results[0];
    // Only allow the first choice
    auto crucial_choice = crucial_result.choices[0];
    auto guess = crucial_choice.text;
    auto parsed = parse_guessed_string(guess);
    for (const auto& viable: label.allowed_strings) {
        if (viable == parsed) {
            return true;
        }
    }
    return false;
}

inline std::optional<float> get_logprob(std::string key, float value, const Label& label) {
    auto parsed_token = parse_guessed_string(key);
    for (const auto& allowed_str: label.allowed_strings) {
        if (parsed_token == allowed_str) {
            return value;
        }
    }
    return std::nullopt;
}

// TODO: Will eventually want to consider logprobs for answers
//       to calculate scores for probabilities for stuff like F1
//       down the line rather than only getting right/wrong tallied
//       For instance, if answer to a question was given "Yes", but "No"
//       was in the top logprobs, it would be useful to get the probs
//       for both down the line
bool guessed_correctly(LabelStates state, const RequestResult& res) {
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
    auto benchmark_duration = metrics.benchmark_end - metrics.benchmark_start;
    auto seconds = duration_cast<std::chrono::duration<double>>(benchmark_duration).count();
    double ttft_sum = 0;
    double e2e_latency_sum = 0;
    double guessed_correct = 0;
    for (const auto& result : metrics.req_results) {
        ttft_sum += result.latencies.ttft;
        e2e_latency_sum += result.latencies.end_to_end_latency;
        if (result.guessed_correctly == true) {
            guessed_correct++;
        }
    }
    auto avg_ttft = ttft_sum / metrics.requests_processed;
    auto avg_e2e_latency = e2e_latency_sum / metrics.requests_processed;
    auto accuracy = guessed_correct / metrics.requests_processed;

    Logger.info(std::format("{} requests processed in {:3f}s, {:.3f} reqs/sec"
                            " | Average TTFT: {:.3f}s"
                            " | Average End-to-End Latency: {:.3f}s"
                            " | Accuracy: {:.2f}%", metrics.requests_processed,
                            seconds, metrics.requests_processed / seconds,
                            avg_ttft, avg_e2e_latency, accuracy));
}

YesNoLogprobPair get_yes_no_logprobs(LabelStates state, bool correct, const RequestResult& res) {
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
        for (const auto& [k,v]: top_logprob) {
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