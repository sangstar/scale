//
// Created by Sanger Steel on 5/7/25.
//

#include "curl.hpp"
#include "benchmarks/benchmark.hpp"
#include <thread>
#include <fstream>
#include <random>
#include "logger.hpp"


constexpr size_t data_token_len = std::string("data:").size();


size_t write_cb_default(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string *) userp)->append((char *) contents, size * nmemb);
    return size * nmemb;
}

size_t write_cb_to_queue(void* contents, size_t size, size_t nmemb, void* userp) {
    auto idx = Logger.set_start();
    auto as_streaming_resp = (StreamingResponse *) userp;
    auto content = std::string((char *) contents, size * nmemb);
    push_chunks(as_streaming_resp, std::move(content));
    Logger.set_stop_and_display_time(idx, "write_cb_to_queue");
    return size * nmemb;
}

CURLHandler::CURLHandler(const char* uri, const char* api_key) {
    this->uri = std::string(uri);
    if (!api_key) {
        throw std::runtime_error("No api key provided.");
    }
    this->api_key = std::string(api_key);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string token_header = std::format("Authorization: Bearer {}", this->api_key);
    headers = curl_slist_append(headers, token_header.c_str());
}

std::string CURLHandler::get(const char* query) {
    CURL* ephemeral = curl_easy_init();
    std::string response;

    curl_easy_setopt(ephemeral, CURLOPT_URL, query);
    curl_easy_setopt(ephemeral, CURLOPT_WRITEFUNCTION, write_cb_default);
    curl_easy_setopt(ephemeral, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(ephemeral);
    if (res != CURLE_OK)
        std::cerr << "curl error: " << curl_easy_strerror(res) << "\n";

    curl_easy_cleanup(ephemeral);

    if (response.empty()) {
        throw std::runtime_error("No response.");
    }

    return response;
}

std::shared_ptr<StreamingResponse> CURLHandler::post_stream(RequestParameters& req) {
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
            double namelookup_time, connect_time, starttransfer_time, total_time;
            curl_easy_setopt(ephemeral, CURLOPT_URL, this->uri.c_str());
            curl_easy_setopt(ephemeral, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(ephemeral, CURLOPT_POST, 1L);
            curl_easy_setopt(ephemeral, CURLOPT_POSTFIELDS, post_data->c_str());
            curl_easy_setopt(ephemeral, CURLOPT_POSTFIELDSIZE, post_data->size());
            curl_easy_setopt(ephemeral, CURLOPT_WRITEFUNCTION, write_cb_to_queue);
            curl_easy_setopt(ephemeral, CURLOPT_WRITEDATA, resp.get());
            auto idx = Logger.set_start();
            resp->start = std::chrono::high_resolution_clock::now();
            auto res = curl_easy_perform(ephemeral);
            curl_easy_getinfo(ephemeral, CURLINFO_NAMELOOKUP_TIME, &namelookup_time);
            curl_easy_getinfo(ephemeral, CURLINFO_CONNECT_TIME, &connect_time);
            curl_easy_getinfo(ephemeral, CURLINFO_STARTTRANSFER_TIME, &starttransfer_time);
            curl_easy_getinfo(ephemeral, CURLINFO_TOTAL_TIME, &total_time);
            resp->latencies.ttft = starttransfer_time;
            resp->latencies.end_to_end_latency = total_time;
            Logger.set_stop_and_display_time(idx, "e2e from server");
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
    resp->t = std::move(t);
    return resp;
}

LatencyMetrics CURLHandler::await(std::shared_ptr<StreamingResponse> resp) {
    if (!resp) {
        throw std::runtime_error("response stream is null");
    }
    if (!resp->done) {
        resp->t.join();
    }
    return resp->latencies;
}

RingState CURLHandler::fetch(std::shared_ptr<StreamingResponse> resp, std::string*& to_write_to) {
    if (resp) {
        return resp->ring.fetch(to_write_to);
    }
    throw std::runtime_error("Invalid buffer.");
}

bool CURLHandler::write_to_buffer_finished(std::shared_ptr<StreamingResponse> resp) {
    if (resp) {
        return resp->ring.producer_finished;
    }
    return false;
}

bool str_contains(const std::string& str, const std::string& to_test) {
    auto comparison_window = to_test.size();
    std::string might_be_chunk;

    for (int i = 0; i < str.size(); i++) {
        might_be_chunk = str.substr(i, comparison_window);
        if (might_be_chunk == to_test) {
            return true;
        }
    }
    return false;
}

// TODO: Refactor this to use str_contains
//       this sometimes breaks
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
        if (found_start_of_chunk && content[i] == '}' && content[i + 1] == '\n') {
            // Need to increment this by one to avoid an off-by-one with substringing
            end = i + 1;
        }
    }
    if (start != 0 && end != 0 && start == end) {
        throw std::runtime_error("chunk failed to process");
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
        auto chunk = content.substr(start, offset - start);

        // TODO: This is brittle to some weird case where "[DONE]" is included in the response text
        // The stopping condition here is when there's nothing left to read in to a chunk or there's a chunk with
        // the [DONE] token
        if (str_contains(chunk, done_token) || start == offset) {
            // TODO: Need to seriously consider some timer abstraction to ensure
            //       processing stuff isn't significantly inflating the actual latency.
            //       At very least must heavily profile
            done = true;
            continue;
        }
        auto msg = std::format("Got chunk: {}", chunk);
        Logger.write(msg.c_str());
        // This is illegal if the above condition is false
        if (chunk.back() != '}') {
            throw std::runtime_error("chunking error");
        }
        streamed->ring.push(std::move(chunk));
    }
    return;
}

json parse_to_json(std::string json_str) {
    json as_json = json::parse(std::move(json_str));
    return as_json;
}
