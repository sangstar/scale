//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <atomic>
#include <optional>
#include <string>
#include <array>

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

template <typename T>
struct MPSCRingBuffer {
    std::atomic<int> head;
    int tail;
    ~MPSCRingBuffer() = default;
    MPSCRingBuffer() = default;
    std::array<Slot<T>, RequestRingBufferMaxSize> data;
    RingState push(T content);
    RingResult<T> fetch();
};


template <typename T>
RingState MPSCRingBuffer<T>::push(T content) {
    auto polled_head = head.load(std::memory_order_acquire);
    auto idx = polled_head % data.size();
    if ((polled_head + 1) % data.size() == tail) {
        return FULL;
    }

    // Check if the slot at idx has been filled yet. If it hasn't,
    // atomically claim and write to it
    if (head.compare_exchange_strong(polled_head, polled_head + 1)) {
        auto& slot = data[idx];
        slot.content = content;

        // slot.ready is understood as "filled"
        slot.ready.store(true, std::memory_order_release);
        return SUCCESS;
    }
    return NOT_READY;
}

template <typename T>
RingResult<T> MPSCRingBuffer<T>::fetch() {
    auto idx = tail % data.size();
    if (tail == head.load(std::memory_order_acquire)) {
        return RingResult<T>{EMPTY, std::nullopt};
    }
    if (data[idx].ready.load(std::memory_order_acquire)) {
        tail++;
        return RingResult<T>{SUCCESS, data[idx].content};
    }
    return RingResult<T>{NOT_READY, std::nullopt};
}