//
// Created by Sanger Steel on 5/23/25.
//

#include "utils.hpp"
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

inline std::string trim_and_lower(std::string& guess) {
    auto trimmed = trim(guess);
    lower(trimmed);
    return std::move(trimmed);
}


inline std::optional<float> get_logprob(std::string& key, float value, std::string& to_compare) {
    auto parsed_token = trim_and_lower(key);
    if (key == trim_and_lower(to_compare)) {
        return value;
    }
    return std::nullopt;
}

// TODO: Will eventually want to consider logprobs for answers
//       to calculate scores for probabilities for stuff like F1
//       down the line rather than only getting right/wrong tallied
//       For instance, if answer to a question was given "Yes", but "No"
//       was in the top logprobs, it would be useful to get the probs
//       for both down the line
bool guessed_correctly(const Dataset& dataset, const RequestResult& res) {
    auto crucial_result = res.completion_results[0];
    // Only allow the first choice
    auto crucial_choice = crucial_result.choices[0];
    auto guess = crucial_choice.text;
    auto parsed = trim_and_lower(guess);

    std::optional<int> guessed_label = std::nullopt;
    auto& cfg = dataset->get_config();
    for (const auto& value: cfg.label.values) {
        if (parsed == value.response) {
            guessed_label = value.id;
        }
    }
    return guessed_label.has_value() && guessed_label == res.params.golden_label;
}

FinalMetrics get_results(Metrics& metrics) {
    auto benchmark_duration = metrics.benchmark_end - metrics.benchmark_start;
    auto seconds = duration_cast<std::chrono::duration<double>>(benchmark_duration).count();
    double ttft_sum = 0;
    double e2e_latency_sum = 0;
    double guessed_correct = 0;
    for (const auto& result: metrics.req_results) {
        ttft_sum += result.latencies.ttft;
        e2e_latency_sum += result.latencies.end_to_end_latency;
        if (result.guessed_correctly == true) {
            guessed_correct++;
        }
    }
    FinalMetrics fm{};
    auto avg_ttft = ttft_sum / metrics.requests_processed;
    auto avg_e2e_latency = e2e_latency_sum / metrics.requests_processed;
    auto accuracy = guessed_correct / metrics.requests_processed * 100;
    fm.avg_ttft = avg_ttft;
    fm.avg_e2e_latency = avg_e2e_latency;
    fm.duration = seconds;
    fm.requests_processed = metrics.requests_processed;
    fm.req_rate = metrics.requests_processed / seconds;
    fm.accuracy = accuracy;


    Logger.info(fm.display());
    Logger.dump_debugging_state();
    return fm;
}

// Only take the highest logprob for each match of the possible response labels
std::vector<logprob_entry> filter_label_logprobs(const std::vector<logprob_entry>& logprob_entries, Config& cfg) {
    std::vector<logprob_entry> filtered_entries;
    for (const auto& value: cfg.label.values) {
        float highest_logprob_for_value = -500;
        size_t idx_with_highest_logprob_for_value = 0;
        for (size_t i = 0; i < logprob_entries.size(); ++i) {
            const logprob_entry& entry = logprob_entries[i];
            if (entry.text == value.response && entry.logprob > highest_logprob_for_value) {
                idx_with_highest_logprob_for_value = i;
                highest_logprob_for_value = entry.logprob.value();
            }
        }
        filtered_entries.emplace_back(logprob_entries[idx_with_highest_logprob_for_value]);
    }
    return std::move(filtered_entries);
}

std::optional<std::vector<logprob_entry>> get_label_logprobs(
    const Dataset& dataset, bool correct, const RequestResult& res
) {
    std::vector<logprob_entry> label_logprobs;
    auto logprobs = res.completion_results[0].choices[0].logprobs;
    if (correct) {
        auto correct_guess_logprob = res.completion_results[0].choices[0].logprobs.token_logprobs[0];
        auto correct_guess_logprob_text = res.completion_results[0].choices[0].logprobs.tokens[0];
        label_logprobs.emplace_back(logprob_entry{correct_guess_logprob_text, correct_guess_logprob});
    }
    // TODO: Need to order the TopLogprobs in descending order, otherwise I risk saying the logprob for
    //       "No" is based off a TopLogprob " NO" with logprob -15.242 when there was a "no" logprob with -2,
    //       which would deflate the performance eval here.
    auto& cfg = dataset->get_config();
    for (auto& top_logprob: logprobs.top_logprobs) {
        for (auto& [k,v]: top_logprob) {
            std::string key = k;
            for (auto& response_label: cfg.label.values) {
                auto maybe_got_logprob = get_logprob(key, v, response_label.response);
                if (maybe_got_logprob.has_value()) {
                    label_logprobs.emplace_back(logprob_entry{response_label.response, maybe_got_logprob.value()});
                }
            }
        }
    }
    if (!label_logprobs.empty()) {
        return filter_label_logprobs(label_logprobs, cfg);
    }
    return std::nullopt;
}

std::vector<json> get_output_json(RequestResult& res, const Dataset& dataset) {
    std::vector<json> json_vec;
    auto logprobs_for_labels = get_label_logprobs(dataset, res.guessed_correctly, res);
    for (const auto& compl_result: res.completion_results) {
        json j = json::object();
        j["e2e_latency"] = res.latencies.end_to_end_latency;
        j["ttft"] = res.latencies.ttft;
        j["id"] = compl_result.id;
        j["model"] = compl_result.model;
        j["object"] = compl_result.object;
        j["prompt"] = res.params.prompt;
        j["guessed_correctly"] = res.guessed_correctly;
        if (logprobs_for_labels.has_value()) {
            auto logprob_labels = logprobs_for_labels.value();
            for (const auto& label_logprobs: logprob_labels) {
                j[label_logprobs.text + "_logprob"] = label_logprobs.logprob;
            }
        }

        auto choice = compl_result.choices[0];
        j["finish_reason"] = choice.finish_reason;
        j["text"] = choice.text;
        json_vec.emplace_back(j);
    }
    return std::move(json_vec);
}

std::string join(std::vector<const std::string>& strings, const std::string& sep) {
    std::string output;
    size_t num_strings = strings.size();
    for (size_t i = 0; i < num_strings; ++i) {
        output += strings[i];
        if (i != num_strings - 1) {
            output += sep;
        }
    }
    return std::move(output);
}
