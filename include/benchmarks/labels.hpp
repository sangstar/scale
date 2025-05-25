//
// Created by Sanger Steel on 5/21/25.
//

#pragma once
#include <vector>

enum LabelStates {
    NO_LABEL,
    YES,
    NO,
};

using LabelStatesMapping = const std::pair<std::string_view, LabelStates>;

// Increase the magic number of 3 here as needed, and just add the extra ones manually
struct Label {
    LabelStates state;
    const std::array<std::string_view, 3> allowed_strings;

    constexpr Label(LabelStates state_, std::array<std::string_view, 3> allowed_strings_) : allowed_strings(
            allowed_strings_),
        state(state_) {
    };
};

constexpr Label YesLabel(YES, {"y", "yes", "yea"});
constexpr Label NoLabel(NO, {"n", "no", "Nope"});

