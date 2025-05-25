//
// Created by Sanger Steel on 5/20/25.
//

#include "benchmarks/cola.hpp"

RequestParameters ColaBenchmark::request_from_dataset_row(int idx) {
    RequestParameters req;
    auto row = dataset.rows[idx];
    req.golden_label = row.label;
    req.prompt = std::format(pre_formatted_text, row.sentence);
    return req;
}

size_t ColaBenchmark::size() const {
    return dataset.rows.size();
}
