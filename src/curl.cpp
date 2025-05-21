//
// Created by Sanger Steel on 5/7/25.
//

#include "curl.hpp"
#include "benchmarks/benchmark.hpp"
#include <thread>
#include <fstream>
#include <random>

constexpr size_t data_token_len = std::string("data:").size();


size_t write_cb_default(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

size_t write_cb_to_queue(void* contents, size_t size, size_t nmemb, void* userp) {
    auto as_streaming_resp = (StreamingResponse*)userp;
    auto content = std::string((char*)contents, size * nmemb);
    if (!as_streaming_resp->got_ttft) {
        auto got_token_time = std::chrono::high_resolution_clock::now();
        as_streaming_resp->latencies.ttft = got_token_time - as_streaming_resp->start;
    }
    push_chunks(as_streaming_resp, std::move(content));
    return size * nmemb;
}

CURLHandler::CURLHandler(const char *uri, const char *api_key) {
    this->uri = std::string(uri);
    this->api_key = std::string(api_key);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string token_header = std::format("Authorization: Bearer {}", this->api_key);
    headers = curl_slist_append(headers, token_header.c_str());
}

std::string CURLHandler::get(const char* query) {
    CURL *ephemeral = curl_easy_init();
    std::string response;

    curl_easy_setopt(ephemeral, CURLOPT_URL, query);
    curl_easy_setopt(ephemeral, CURLOPT_WRITEFUNCTION, write_cb_default);
    curl_easy_setopt(ephemeral, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(ephemeral);
    if (res != CURLE_OK)
        std::cerr << "curl error: " << curl_easy_strerror(res) << "\n";

    curl_easy_cleanup(ephemeral);
    return response;
}

std::thread::id CURLHandler::post_stream(RequestParameters& req) {
    auto post_data = std::make_shared<std::string>(req.to_json().dump());
    auto resp = std::make_shared<StreamingResponse>();
    resp->start = std::chrono::high_resolution_clock::now();
    resp->got_ttft = false;

    // TODO: Processing can inflate the "true" benchmarking numbers. Figure out how to resolve this
    //       either by taking more measurements that can exclude the processing time, or something
    //       else
    std::thread t(
        [post_data, resp, this] {
            CURL* ephemeral = curl_easy_init();
            curl_easy_setopt(ephemeral, CURLOPT_URL, this->uri.c_str());
            curl_easy_setopt(ephemeral, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(ephemeral, CURLOPT_POST, 1L);
            curl_easy_setopt(ephemeral, CURLOPT_POSTFIELDS, post_data->c_str());
            curl_easy_setopt(ephemeral, CURLOPT_POSTFIELDSIZE, post_data->size());
            curl_easy_setopt(ephemeral, CURLOPT_WRITEFUNCTION, write_cb_to_queue);
            curl_easy_setopt(ephemeral, CURLOPT_WRITEDATA, resp.get());
            auto res = curl_easy_perform(ephemeral);
            if (res != CURLE_OK) {
                // TODO: C-style error here is weird
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                exit(1);
            }
            resp->ring.producer_finished = true;
            resp->done = true;

            curl_easy_cleanup(ephemeral);
    }
    );
    auto id = t.get_id();
    resp->t = std::move(t);
    buf[id] = resp;
    return id;
}

LatencyMetrics CURLHandler::await(std::thread::id req_id) {
    auto resp = buf[req_id].get();
    if (!resp->done) {
        resp->t.join();
    }
    return resp->latencies;
}

RingResult<std::string> CURLHandler::fetch(std::thread::id req_id) {
    return buf[req_id].get()->ring.fetch();
}

bool CURLHandler::write_to_buffer_finished(std::thread::id id) {
    return this->buf[id]->ring.producer_finished;
}

bool str_contains(const std::string& str, const std::string& to_test) {
    auto comparison_window = to_test.size();
    std::string might_be_chunk;

    for (int i = 0; i < str.size(); i++) {
        might_be_chunk = str.substr(i, i + comparison_window);
        if (might_be_chunk == to_test) {
            return true;
        }
    }
    return false;
}

// TODO: Refactor this to use str_contains
std::tuple<int, int> get_chunk_indices(int offset, const std::string& content) {
    int start = 0;
    int end = 0;
    bool found_start_of_chunk = false;
    std::string might_be_chunk;

    for (size_t i = offset; i < content.size(); i++) {
        if (end != 0) {
            break;
        }
        if (!found_start_of_chunk && might_be_chunk == "data:") {
            found_start_of_chunk = true;
            start = i + data_token_len;
        }
        if (!found_start_of_chunk) {
            // Look ahead 5 bytes to see if the collection matches "data:"
            if (i + 5 <= content.size()) {
                might_be_chunk = content.substr(i, 5);
            }
        }
        if (found_start_of_chunk && content[i] == '}' && content[i+1] == '\n') {
            // Need to increment this by one to avoid an off-by-one with substringing
            end = i + 1;
        }
    }
    return std::tuple<int, int>(start, end);
}

void push_chunks(StreamingResponse* streamed, std::string content) {
    bool done = false;
    int offset = 0;
    std::string done_token = "[DONE]";
    while (!done) {
        auto t = get_chunk_indices(offset, content);
        auto start = std::get<0>(t);
        offset = std::get<1>(t);
        if (offset == 0) {
            done = true;
            continue;
        }
        auto chunk = content.substr(start, offset - start);
        if (chunk.back() != '}') {
            throw std::runtime_error("chunking error");
        }
        // TODO: This is brittle to some weird case where "[DONE]" is included in the response text
        if (!str_contains(chunk, done_token)) {
            streamed->ring.push(std::move(chunk));
        } else {
            streamed->latencies.end_to_end_latency = std::chrono::high_resolution_clock::now() - streamed->start;
            done = true;
        }
    }
    return;
}

json parse_to_json(std::string json_str) {
    json as_json = json::parse(std::move(json_str));
    return as_json;
}
