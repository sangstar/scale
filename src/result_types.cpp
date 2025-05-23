//
// Created by Sanger Steel on 5/20/25.
//

#include "result_types.hpp"

std::vector<json> RequestResults::to_json() {
    std::vector<json> json_vec;
    for (auto& compl_result : completion_results) {
        json j;
        j["e2e_latency"] = latencies.end_to_end_latency.count();
        j["ttft"] = latencies.ttft.count();
        j["id"] = compl_result.id;
        j["model"] = compl_result.model;
        j["object"] = compl_result.object;
        j["prompt"] = params.prompt;
        int choice_count = 0;
        for (auto& choice : compl_result.choices) {
            std::string choice_id = "choice_" + std::to_string(choice_count);
            j[choice_id + "_finish_reason"] = choice.finish_reason;
            j[choice_id + "_text"] = choice.text;
        }
        json_vec.emplace_back(j);
    }
    return std::move(json_vec);
}
