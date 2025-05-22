//
// Created by Sanger Steel on 5/7/25.
//

#pragma once
#include <string>
#include <curl/curl.h>
#include <format>
#include <iostream>
#include <thread>
#include "../external/json.hpp"
#include "ring_buffers.hpp"
#include "request_parameters.hpp"
#include "latency_metrics.hpp"
#include "streaming_response.hpp"

using json = nlohmann::json;

using ResponseBuffer = std::unordered_map<std::thread::id, std::shared_ptr<StreamingResponse>>;

static size_t write_cb_default(void* contents, size_t size, size_t nmemb, void* userp);

static size_t write_cb_to_queue(void* contents, size_t size, size_t nmemb, void* userp);


// TODO: libcurl with libcurl_easy_perform might hit a ceiling at some point
//       with a large number of concurrent requests, although GPU VRAM will probably
//       bottleneck first
class CURLHandler {
public:
    std::string uri;
    explicit CURLHandler(const char* uri, const char* api_key = "");
    static std::string get(const char* query);
    std::thread::id post_stream(RequestParameters& req);
    LatencyMetrics await(std::thread::id req_id);
    RingResult<std::string> fetch(std::thread::id req_id);
    bool write_to_buffer_finished(std::thread::id);

private:
    std::string api_key;
    curl_slist *headers = nullptr;
    ResponseBuffer buf;
};

json parse_to_json(std::string json_str);

bool str_contains(const std::string& str, const std::string& to_test);

void push_chunks(StreamingResponse* streamed, std::string content);
