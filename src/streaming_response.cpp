//
// Created by Sanger Steel on 6/6/25.
//

#include "streaming_response.hpp"

bool StreamingResponse::ready_to_fetch() const {
    return fetchable.load(std::memory_order_acquire);
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

