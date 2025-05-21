//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <atomic>
#include <optional>
#include <string>
#include "results.hpp"

constexpr int RequestRingBufferMaxSize = 1000;
constexpr int ResultsRingBufferMaxSize = 1'000'000;

enum RingState {
    FULL,
    EMPTY,
    SUCCESS,
    NOT_READY,
};

template <typename T>
struct RingResult {
    RingState state;
    std::optional<T> content;
};

template <typename T>
struct Slot {
    std::atomic<bool> ready;
    T content;
};

struct SPMCRingBuffer {
    int head;
    std::atomic<int> tail;
    bool producer_finished; // single producer mutates this, not atomic
    ~SPMCRingBuffer() = default;
    SPMCRingBuffer() = default;
    std::array<Slot<std::string>, RequestRingBufferMaxSize> data;
    RingState push(std::string content);
    RingResult<std::string> fetch();
};

struct MPSCRingBuffer {
    std::atomic<int> head;
    int tail;
    ~MPSCRingBuffer() = default;
    MPSCRingBuffer() = default;
    std::array<Slot<Results>, RequestRingBufferMaxSize> data;
    RingState push(Results content);
    RingResult<Results> fetch();
};