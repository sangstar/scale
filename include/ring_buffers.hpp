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

template<typename T>
struct RingResult {
    RingState state;
    T* content;
};

template<typename T>
struct Slot {
    std::atomic<bool> ready;
    T content;
};

template <typename T>
struct SPMCRingBuffer {
    int head;
    std::atomic<int> tail;
    bool producer_finished;
    ~SPMCRingBuffer() = default;

    SPMCRingBuffer() = default;

    std::array<Slot<std::string>, RequestRingBufferMaxSize> data;

    RingState push(T content) {
        {
            // Single producer
            auto idx = head % data.size();
            auto next_head = head + 1;
            if (next_head == tail.load(std::memory_order_acquire)) {
                // Full
                return FULL;
            }
            data[idx].content = std::move(content);
            data[idx].ready.store(true, std::memory_order_release);
            head++;
            return SUCCESS;
        }
    }

    RingState fetch(T*& item) {
        auto polled_tail = tail.load(std::memory_order_acquire);
        if (polled_tail == head) {
            item = nullptr;
            return EMPTY;
        }
        auto idx = polled_tail % data.size();
        const auto& slot = data[idx];
        if (slot.ready.load(std::memory_order_acquire)) {
            if (tail.compare_exchange_strong(polled_tail, polled_tail + 1)) {
                item = &data[idx].content;
                return SUCCESS;
            }
        }
        item = nullptr;
        return NOT_READY;
    }
};

template<typename T>
struct MPSCRingBuffer {
    std::atomic<int> head;
    int tail;

    ~MPSCRingBuffer() = default;

    MPSCRingBuffer() = default;

    std::array<Slot<T>, RequestRingBufferMaxSize> data;

    RingState push(T content);

    RingState fetch(T*& item);
};


template<typename T>
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

template<typename T>
RingState MPSCRingBuffer<T>::fetch(T*& item) {
    auto idx = tail % data.size();
    if (tail == head.load(std::memory_order_acquire)) {
        item = nullptr;
        return EMPTY;
    }
    if (data[idx].ready.load(std::memory_order_acquire)) {
        tail++;
        item = &data[idx].content;
        return SUCCESS;
    }
    item = nullptr;
    return NOT_READY;
}
