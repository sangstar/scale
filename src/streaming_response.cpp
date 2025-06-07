//
// Created by Sanger Steel on 6/6/25.
//

#include "streaming_response.hpp"

bool StreamingResponse::check_producer_finished() {
    return ring.producer_finished;
}

bool StreamingResponse::ready_to_fetch() const {
    auto can_fetch = fetchable.load(std::memory_order_acquire);
    return can_fetch || done; // done used here so threads can stop waiting and end
}

void StreamingResponse::finalize() {
    std::lock_guard lock(mu);
    ring.producer_finished = true;
    done = true;
    cv.notify_all();
}

void StreamingResponse::push(std::string str) {
    RingState state = ring.push(std::move(str));
    if (state == SUCCESS) {
        std::lock_guard<std::mutex> lock(mu);
        auto expected = false;
        fetchable.compare_exchange_strong(expected, true, std::memory_order_release);
        cv.notify_all();
        return;
    }
    auto expected = true;
    fetchable.compare_exchange_strong(expected, false, std::memory_order_release);
}

RingState StreamingResponse::fetch(std::string*& item) {
    return ring.fetch(item);
}

