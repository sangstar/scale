//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <atomic>
#include <optional>
#include <string>
#include <array>

constexpr int RequestRingBufferMaxSize = 10'000;
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
    std::optional<T> content;
};

template<typename T>
struct Slot {
    std::atomic<bool> ready;
    T content;
};


struct PolledIdx {
    size_t polled_idx;
    size_t idx_in_buffer;
    size_t next_idx;
};

template <typename T>
struct FetchResult {
    T*& item;
    T& to_assign;
};

template <typename T, std::size_t N>
struct RingBuffer {
    virtual ~RingBuffer() = default;

    std::atomic<size_t> head;
    std::atomic<size_t> tail;

    std::array<Slot<T>, N> data;

    std::optional<PolledIdx> get_idx_and_next_head_or_full() {
        auto polled_head = this->head.load(std::memory_order_relaxed);
        auto idx = polled_head % data.size();
        auto next_head = polled_head + 1;
        if (next_head == this->tail.load(std::memory_order_acquire)) {
            // Implies buffer is FULL
            return std::nullopt;
        }
        return PolledIdx{polled_head, idx, next_head};
    }

    std::optional<PolledIdx> get_idx_and_next_tail_or_empty() {
        auto polled_tail = this->tail.load(std::memory_order_acquire);
        if (polled_tail == this->head) {
            // Implies buffer is EMPTY
            return std::nullopt;
        }
        auto idx = polled_tail % data.size();
        return PolledIdx(polled_tail, idx, polled_tail + 1);
    }


    void send_to_slot(PolledIdx head_state, T&& content) {
        data[head_state.idx_in_buffer].content = std::move(content);
        data[head_state.idx_in_buffer].ready.store(true, std::memory_order_release);
        this->head.store(head_state.next_idx, std::memory_order_relaxed);
    }


};

template <typename T>
struct SPMCRingBuffer : RingBuffer<T, RequestRingBufferMaxSize> {

    bool producer_finished = false;
    ~SPMCRingBuffer() = default;
    std::atomic<bool> fetchable;

    SPMCRingBuffer() = default;

    RingState push(T content) {
            // Single producer
            auto maybe_head = this->get_idx_and_next_head_or_full();
            if (!maybe_head.has_value()) {
                return FULL;
            }
            this->send_to_slot(
                std::move(maybe_head.value()),
                std::move(content)
                );
            return SUCCESS;
    }

    RingState fetch(T*& item) {
        auto maybe_tail = this->get_idx_and_next_tail_or_empty();
        if (!maybe_tail.has_value()) {
            return EMPTY;
        }
        auto claimed_tail = std::move(maybe_tail.value());
        const auto& slot = this->data[claimed_tail.idx_in_buffer];
        if (slot.ready.load(std::memory_order_acquire)) {
            if (this->tail.compare_exchange_strong(claimed_tail.polled_idx, claimed_tail.next_idx)) {
                item = std::move(&this->data[claimed_tail.idx_in_buffer].content);
                return SUCCESS;
            }
        }
        return NOT_READY;
    }
};

template<typename T>
struct MPSCRingBuffer : RingBuffer<T, ResultsRingBufferMaxSize> {

    ~MPSCRingBuffer() = default;
    MPSCRingBuffer() = default;


    RingState push(T&& content);

    // Writes the fetched data to `item`
    // if there is data to fetch. If not,
    // leaves it untouched
    RingState fetch(T*& item);

    // Returns a copy of the fetched value -- safer
    RingResult<T> fetch();
};


template<typename T>
RingState MPSCRingBuffer<T>::push(T&& content) {
    auto maybe_head = this->get_idx_and_next_head_or_full();
    if (!maybe_head.has_value()) {
        return FULL;
    }
    auto claimed_head = std::move(maybe_head.value());

    // Check if the slot at idx has been filled yet. If it hasn't,
    // atomically claim and write to it
    if (this->head.compare_exchange_strong(claimed_head.polled_idx, claimed_head.next_idx)) {
        auto& slot = this->data[claimed_head.idx_in_buffer];
        slot.content = std::move(content);

        // slot.ready is understood as "filled and ready to ready from"
        slot.ready.store(true, std::memory_order_release);
        return SUCCESS;
    }
    return NOT_READY;
}

template<typename T>
RingState MPSCRingBuffer<T>::fetch(T*& item) {
    auto maybe_tail = this->get_idx_and_next_tail_or_empty();
    if (!maybe_tail.has_value()) {
        return EMPTY;
    }
    auto claimed_tail = std::move(maybe_tail.value());

    if (this->data[claimed_tail.idx_in_buffer].ready.load(std::memory_order_acquire)) {
        this->tail.store(claimed_tail.next_idx, std::memory_order_relaxed);
        item = std::move(&this->data[claimed_tail.idx_in_buffer].content);
        return SUCCESS;
    }
    return NOT_READY;
}

template<typename T>
RingResult<T> MPSCRingBuffer<T>::fetch() {
    auto maybe_tail = this->get_idx_and_next_tail_or_empty();
    if (!maybe_tail.has_value()) {
        return RingResult<T>{EMPTY, {}};
    }
    auto claimed_tail = std::move(maybe_tail.value());
    if (this->data[claimed_tail.idx_in_buffer].ready.load(std::memory_order_acquire)) {
        T content = std::move(this->data[claimed_tail.idx_in_buffer].content);
        this->tail.store(claimed_tail.next_idx, std::memory_order_relaxed);
        return RingResult<T>{SUCCESS, std::move(content)};
    }
    return RingResult<T>{NOT_READY, {}};
}
