//
// Created by Sanger Steel on 5/20/25.
//

#pragma once
#include <atomic>
#include <optional>
#include <string>
#include <array>
#include <execinfo.h>
#include <thread>
#include <iostream>
#include <unistd.h>
#include <__format/format_functions.h>

static std::mutex debugger_mutex;

constexpr int RequestRingBufferMaxSize = 10'000;
constexpr int ResultsRingBufferMaxSize = 1'000'000;

inline void print_stacktrace() {
    void* callstack[128];
    int frames = backtrace(callstack, 128);
    backtrace_symbols_fd(callstack, frames, STDERR_FILENO);
}

enum class SlotState {
    VACANT,
    WRITING,
    WRITTEN,
    READING,
};

inline const char* slot_state_as_str(const SlotState& state) {
    switch (state) {
        case SlotState::VACANT: return "VACANT";
        case SlotState::WRITING: return "WRITING";
        case SlotState::WRITTEN: return "WRITTEN";
        case SlotState::READING: return "READING";
        default: return "INVALID";
    }
}

enum class ChangeStateReason {
    UNCHANGED,
    READ_CLAIMED_AND_SET_TO_VACANT,
    WRITE_TO_SLOT_FINISHED,
    CLAIM_VACANT_TO_WRITE,
    CLAIM_WRITTEN_TO_FETCH,
    CLAIMED_SET_TO_READING,
    READ_FINISHED_AND_SET_TO_VACANT,
    CLAIM_SLOT_ON_PROTECTED_VALUE_INIT
};

enum class RingState {
    FULL,
    EMPTY,
    SUCCESS,
    NOT_READY,
};

struct PolledIdx {
    size_t polled_idx;
    size_t idx_in_buffer;
    size_t next_idx;
};

template<typename T>
struct RingResult {
    RingState state;
    std::optional<T> content;
    int slot_idx;

    RingResult(RingState state_, std::optional<T> content_, int idx)
    : state(state_), content(std::move(content_)), slot_idx(idx) {}
};


class StateChangeRecords {
public:

    static std::string state_as_str(const ChangeStateReason& state) {
        switch (state) {
            case ChangeStateReason::UNCHANGED: return "UNCHANGED";
            case ChangeStateReason::READ_CLAIMED_AND_SET_TO_VACANT: return "READ_CLAIMED_AND_SET_TO_VACANT";
            case ChangeStateReason::WRITE_TO_SLOT_FINISHED: return "WRITE_TO_SLOT_FINISHED";
            case ChangeStateReason::CLAIM_VACANT_TO_WRITE: return "CLAIM_VACANT_TO_WRITE";
            case ChangeStateReason::CLAIM_WRITTEN_TO_FETCH: return "CLAIM_WRITTEN_TO_FETCH";
            case ChangeStateReason::CLAIMED_SET_TO_READING: return "CLAIMED_SET_TO_READING";
            case ChangeStateReason::READ_FINISHED_AND_SET_TO_VACANT: return "READ_FINISHED_AND_SET_TO_VACANT";
            case ChangeStateReason::CLAIM_SLOT_ON_PROTECTED_VALUE_INIT: return "CLAIM_SLOT_ON_PROTECTED_VALUE_INIT";
            default: return "INVALID";
        }
    }

    std::string to_str() {
        std::string s;
        for (const auto& reason : records) {
            s += state_as_str(reason) + ", ";
        }
        return s;
    }

    std::vector<ChangeStateReason> records;

};


class SlotDebugger {
public:
    SlotDebugger() :
#ifdef NDEBUG
        enabled(false) {};
#else
        enabled(true) {};
#endif



    void debug(std::function<void()> fn) {
        if (enabled) {
            std::lock_guard<std::mutex> lock(debugger_mutex);
            fn();
        }
    }


    void get_change_reasons(const int idx) {
        debug([&] {
            std::cout << state_changes.to_str() << std::endl;
        });
    }


    void record_change(SlotState original, SlotState new_state, ChangeStateReason reason, int idx) {
        debug([&] {
            slot_state_changes.fetch_add(1, std::memory_order_acq_rel);
            state_changes.records.emplace_back(reason);
            std::cout << "Slot " << idx << ": changed from " << slot_state_as_str(original) << " to " << slot_state_as_str(new_state) << " with reason " << StateChangeRecords::state_as_str(reason) <<  std::endl;
        });
    }

    void dump_state() {
        std::cout << std::format("Slot state lifetime: {}", state_changes.to_str()) << std::endl;
        print_stacktrace();
    }

    void error(const std::string& msg) {
        debug([&] {
            std::cout << "ERROR ENCOUNTERED!" << std::endl;
            dump_state();
            throw std::runtime_error(msg);
        });
    }

    void clear_change_reason_history() {
        debug([&] {
            state_changes.records.clear();
        });
    }

    template<typename... Ts>
    void print(Ts&&... ts) {
        debug ([&] {
            (std::cout << ... << ts) << std::endl;  // folds ts into (cout << t1 << t2 << …)
        });
    }

    bool enabled;

private:
    std::atomic<int> slot_state_changes = 0;
    std::mutex sentinel_mu;
    StateChangeRecords state_changes;
};

template <typename T>
class Slot {
public:

    bool state_compare_exchange_weak(SlotState& expected, SlotState new_state, ChangeStateReason reason, int idx) {
        bool result = state.compare_exchange_weak(expected, new_state, std::memory_order_acq_rel);
        if (result) { debugger.record_change(expected, new_state, reason, idx); }
        return result;
    }

    bool state_compare_exchange_strong(SlotState& expected, SlotState new_state, ChangeStateReason reason, int idx) {
        bool result = state.compare_exchange_strong(expected, new_state, std::memory_order_acq_rel);
        if (result) { debugger.record_change(expected, new_state, reason, idx); }
        return result;
    }

    const SlotState& read_state() {
        const auto& read = state.load(std::memory_order_acquire);
        return read;
    }

    void set(T& to_set, int idx) {
        while (true) {
            auto expected = SlotState::WRITING;
            debugger.print("Writing to Slot ", idx, " : ", to_set);
            value = to_set;
            if (!state_compare_exchange_strong(expected, SlotState::WRITTEN, ChangeStateReason::WRITE_TO_SLOT_FINISHED, idx)) {
                debugger.error(std::format("Slot {} state has changed to {} while writing", idx, slot_state_as_str(expected)));
            }
            break;
        }
    }

    T get_value(int idx) {

        while (true) {
            SlotState expected = SlotState::READING;
            if (state_compare_exchange_strong(expected, SlotState::VACANT, ChangeStateReason::READ_FINISHED_AND_SET_TO_VACANT, idx)) {
                debugger.clear_change_reason_history();
                debugger.print("Slot ", idx, " returning value ", value);
                return std::move(value);
            }
        }
    }

    SlotDebugger debugger;

private:
    std::atomic<int> state_changes = 0;
    std::atomic<SlotState> state;
    T value;
};

template <typename T, std::size_t N>
struct RingBuffer {
    virtual ~RingBuffer() = default;

    std::atomic<size_t> head;
    std::atomic<size_t> tail;

    std::array<Slot<T>, N> data;

    bool is_empty() const {
        return (tail.load(std::memory_order_acquire) == head.load(std::memory_order_acquire));
    }

    std::optional<PolledIdx> try_claim_head() {
        while (true) {
            size_t t = this->tail.load(std::memory_order_acquire);
            size_t h = this->head.load(std::memory_order_acquire);
            if ((h+1) % N == t % N) {
                return std::nullopt;  // full
            }
            if (this->head.compare_exchange_weak(h, h + 1, std::memory_order_acq_rel)) {
                return PolledIdx{h, h % N, h + 1};
            }
        }
    }

    std::optional<PolledIdx> try_claim_tail() {
        while (true) {
            size_t t = this->tail.load(std::memory_order_acquire);
            size_t h = this->head.load(std::memory_order_acquire);
            if (t == h) {
                return std::nullopt;  // empty
            }
            if (this->tail.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel)) {
                return PolledIdx{t, t % N, t + 1};
            }
        }
    }

    T receive_from_slot(PolledIdx tail_state) {
        auto& slot = data[tail_state.idx_in_buffer];
        while (true) {
            SlotState expected = SlotState::WRITTEN;
            if (slot.state_compare_exchange_weak(expected, SlotState::READING, ChangeStateReason::CLAIM_WRITTEN_TO_FETCH, tail_state.idx_in_buffer)) {
                return slot.get_value(tail_state.idx_in_buffer);
            }
            std::this_thread::yield();
        }
    }


    void send_to_slot(PolledIdx head_state, T&& content) {
        auto& slot = data[head_state.idx_in_buffer];
        while (true) {
            SlotState expected = SlotState::VACANT;
            if (slot.state_compare_exchange_weak(expected, SlotState::WRITING, ChangeStateReason::CLAIM_VACANT_TO_WRITE, head_state.idx_in_buffer)) {
                slot.set(content, head_state.idx_in_buffer);
                break;
            }
            std::this_thread::yield();
        }
    }


};

template <typename T>
struct SPMCRingBuffer : RingBuffer<T, RequestRingBufferMaxSize> {

    bool producer_finished = false;
    ~SPMCRingBuffer() = default;
    std::atomic<bool> fetchable;

    SPMCRingBuffer() = default;

    RingState push(T content) {
        auto maybe_head = this->try_claim_head();
        if (!maybe_head.has_value()) {
            return RingState::FULL;
        }
        this->send_to_slot(
            std::move(maybe_head.value()),
            std::move(content)
            );
        return RingState::SUCCESS;
    }

    RingResult<T> fetch() {
        auto maybe_tail = this->try_claim_tail();
        if (!maybe_tail.has_value()) {
            return RingResult<T>(RingState::EMPTY, std::nullopt, 0);
        }
        auto claimed_tail = std::move(maybe_tail.value());
        return RingResult<T>(RingState::SUCCESS, std::make_optional(this->receive_from_slot(claimed_tail)), claimed_tail.idx_in_buffer);
    }


};

template<typename T>
struct MPSCRingBuffer : RingBuffer<T, ResultsRingBufferMaxSize> {

    ~MPSCRingBuffer() = default;
    MPSCRingBuffer() = default;


    RingState push(T content);

    // Returns a copy of the fetched value -- safer
    RingResult<T> fetch();
};


template<typename T>
RingState MPSCRingBuffer<T>::push(T content) {
    auto maybe_head = this->try_claim_head();
    if (!maybe_head.has_value()) {
        return RingState::FULL;
    }
    auto claimed_head = std::move(maybe_head.value());
    this->send_to_slot(claimed_head, std::move(content));
    return RingState::SUCCESS;
}

template<typename T>
RingResult<T> MPSCRingBuffer<T>::fetch() {
    auto maybe_tail = this->try_claim_tail();
    if (!maybe_tail.has_value()) {
        return RingResult<T>(RingState::EMPTY, std::nullopt, 0);
    }
    auto claimed_tail = std::move(maybe_tail.value());
    return RingResult<T>(RingState::SUCCESS, std::make_optional(this->receive_from_slot(claimed_tail)), claimed_tail.idx_in_buffer);
}
