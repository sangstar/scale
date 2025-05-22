//
// Created by Sanger Steel on 5/20/25.
//

#include "cola.hpp"

RequestParameters ColaBenchmark::request_from_dataset_row(int idx) const {
    RequestParameters req;
    const auto& rows = dataset["rows"];
    const auto& row = rows[idx];
    const auto& row_entry = row["row"];
    if (row_entry["idx"] != idx) {
        throw std::runtime_error("Improperly indexed dataset row.");
    }
    const auto& sentence = row_entry["sentence"].dump();
    const auto& label = row_entry["label"].dump();
    req.golden_label = label;
    req.prompt = std::format(pre_formatted_text, sentence);
    return req;
}

size_t ColaBenchmark::size() {
    auto rows = dataset["rows"];
    return rows.size();
}
