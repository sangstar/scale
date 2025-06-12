//
// Created by Sanger Steel on 6/6/25.
//

#include "streaming_response.hpp"

#include <logger.hpp>

bool StreamingResponse::check_producer_finished() {
    return ring.producer_finished;
}

bool StreamingResponse::ready_to_fetch() const {
    return !this->ring.is_empty() || done; // done used here so threads can stop waiting and end
}

void StreamingResponse::finalize() {
    std::lock_guard lock(mu);
    ring.producer_finished = true;
    done = true;
    cv.notify_all();
}

void StreamingResponse::push(std::string str) {
    RingState state = ring.push(std::move(str));
    if (state == RingState::SUCCESS) {
        Logger.pushed_chunks.fetch_add(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> lock(mu);
        cv.notify_all();
    }
}

RingResult<std::string> StreamingResponse::fetch() {
    return ring.fetch();
}

