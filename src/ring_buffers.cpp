//
// Created by Sanger Steel on 5/20/25.
//

#include "ring_buffers.hpp"


RingState SPMCRingBuffer::push(std::string content) {
    // Single producer
    auto idx = head % data.size();
    auto next_head = head + 1;
    if (next_head == tail.load(std::memory_order_acquire)) {
        // Full
        return FULL;
    }
    data[idx].content = content;
    data[idx].ready.store(true, std::memory_order_release);
    head++;
    return SUCCESS;
}

RingResult<std::string> SPMCRingBuffer::fetch() {
    auto polled_tail = tail.load(std::memory_order_acquire);
    if (polled_tail == head) {
        return RingResult<std::string>{EMPTY, std::nullopt};
    }
    auto idx = polled_tail % data.size();
    const auto& slot = data[idx];
    if (slot.ready.load(std::memory_order_acquire)) {
        if (tail.compare_exchange_strong(polled_tail, polled_tail + 1)) {
            return RingResult<std::string>{SUCCESS, std::move(data[idx].content)};
        }
    }

    return RingResult<std::string>{NOT_READY,std::nullopt};
}

RingState MPSCRingBuffer::push(Results content) {
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

RingResult<Results> MPSCRingBuffer::fetch() {
    auto idx = tail % data.size();
    if (tail == head.load(std::memory_order_acquire)) {
        return RingResult<Results>{EMPTY, std::nullopt};
    }
    if (data[idx].ready.load(std::memory_order_acquire)) {
        tail++;
        return RingResult<Results>{SUCCESS, data[idx].content};
    }
    return RingResult<Results>{NOT_READY, std::nullopt};
}