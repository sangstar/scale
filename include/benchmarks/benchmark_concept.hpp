//
// Created by Sanger Steel on 5/23/25.
//

#pragma once
#include <concepts>
#include "../../external/json.hpp"
#include "../result_types.hpp"
#include "labels.hpp"
#include "benchmark_types.hpp"

using json = nlohmann::json;


// TODO: Add required method for getting default request params for benchmark
template<typename T>
concept Benchmark = requires(T benchmark, int idx, json& row)
{
    { T::pre_formatted_text } -> std::convertible_to<std::string_view>;
    { benchmark.rows } -> std::same_as<Rows&>;
    { benchmark.label_map } -> std::convertible_to<LabelStatesMapping *>;
    { T::prompt_feature_names } -> std::convertible_to<const std::string_view*>;
    { T::class_label_feature_name } -> std::same_as<const std::string_view&>;
    { benchmark.get_prompt(row) } -> std::same_as<std::string>;
};
