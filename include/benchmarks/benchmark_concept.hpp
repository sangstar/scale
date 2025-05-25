//
// Created by Sanger Steel on 5/23/25.
//

#pragma once
#include <concepts>
#include "../../external/json.hpp"
#include "../result_types.hpp"
#include "labels.hpp"
#include "dataset.hpp"

using json = nlohmann::json;


template<typename T>
concept Benchmark = requires(T benchmark, int idx)
{
    { benchmark.request_from_dataset_row(idx) } -> std::same_as<RequestParameters>;
    { T::pre_formatted_text } -> std::convertible_to<std::string_view>;
    { benchmark.dataset } -> std::same_as<Dataset&>;
    { benchmark.label_map } -> std::convertible_to<LabelStatesMapping *>;
};
