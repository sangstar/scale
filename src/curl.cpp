//
// Created by Sanger Steel on 5/7/25.
//

#include "curl.hpp"
#include <thread>
#include <random>
#include "logger.hpp"


constexpr size_t data_token_len = std::string("data:").size();
const std::string done_token = "[DONE]";
const std::string start_token = "{\"";
const std::string end_token = "}\n";
const std::string chunk_start_text = "data: ";

size_t write_cb_default(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string *) userp)->append((char *) contents, size * nmemb);
    return size * nmemb;
}

size_t write_cb_to_queue(void* contents, size_t size, size_t nmemb, void* userp) {
    auto idx = Logger.set_start();
    auto as_streaming_resp = (StreamingResponse *) userp;
    auto content = std::string((char *) contents, size * nmemb);
    Logger.send_chunks_calls.fetch_add(1, std::memory_order_acq_rel);
    push_chunks(as_streaming_resp, std::move(content));
    Logger.set_stop_and_display_time(idx, "write_cb_to_queue");
    return size * nmemb;
}

CURLHandler::CURLHandler(const char* uri, const char* api_key, std::optional<long> timeout) : timeout(timeout) {
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
            bool finished = false;
            while (!finished) {
                CURL* ephemeral = curl_easy_init();
                curl_easy_setopt(ephemeral, CURLOPT_URL, this->uri.c_str());
                curl_easy_setopt(ephemeral, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(ephemeral, CURLOPT_POST, 1L);
                curl_easy_setopt(ephemeral, CURLOPT_POSTFIELDS, post_data->c_str());
                curl_easy_setopt(ephemeral, CURLOPT_POSTFIELDSIZE, post_data->size());
                curl_easy_setopt(ephemeral, CURLOPT_WRITEFUNCTION, write_cb_to_queue);

                if (this->timeout.has_value()) {
                    // Logger.debug("Using timeout: {}", std::to_string(this->timeout.value()));
                    curl_easy_setopt(ephemeral, CURLOPT_TIMEOUT, this->timeout);
                }

                if (Logger.level == DEBUG) {
                    curl_easy_setopt(ephemeral, CURLOPT_VERBOSE, 1L);
                }
                curl_easy_setopt(ephemeral, CURLOPT_WRITEDATA, resp.get());
                auto idx = Logger.set_start();
                resp->start = std::chrono::high_resolution_clock::now();
                auto res = curl_easy_perform(ephemeral);

                double name_lookup, connect, ssl, start_transfer, total;
                curl_easy_getinfo(ephemeral, CURLINFO_NAMELOOKUP_TIME, &name_lookup);
                curl_easy_getinfo(ephemeral, CURLINFO_CONNECT_TIME, &connect);
                curl_easy_getinfo(ephemeral, CURLINFO_APPCONNECT_TIME, &ssl);
                curl_easy_getinfo(ephemeral, CURLINFO_STARTTRANSFER_TIME, &start_transfer);
                curl_easy_getinfo(ephemeral, CURLINFO_TOTAL_TIME, &total);

                resp->latencies.ttft = start_transfer;
                resp->latencies.end_to_end_latency = total;
                Logger.set_stop_and_display_time(idx, "e2e from server");
                Logger.debug(std::format(
                    "timing: DNS={}s, TCP={}s, SSL={}s, TTFT={}s, Total={}s",
                    name_lookup, connect - name_lookup, ssl - connect,
                    start_transfer, total
                ));
                if (res != CURLE_OK) {
                    if (res == CURLE_OPERATION_TIMEDOUT) {
                        Logger.debug("Request timed out, retrying..");
                        curl_easy_cleanup(ephemeral);
                    } else {
                        // TODO: C-style error here is weird
                        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                        exit(1);
                    }
                } else {
                    resp->finalize();
                    curl_easy_cleanup(ephemeral);
                    finished = true;
                }
            }
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

RingResult<std::string> CURLHandler::fetch(const std::shared_ptr<StreamingResponse>& resp) {
    if (resp) {
        return resp->fetch();
    }
    throw std::runtime_error("Invalid buffer.");
}

bool CURLHandler::write_to_buffer_finished(const std::shared_ptr<StreamingResponse>& resp) {
    if (resp) {
        return resp->check_producer_finished();
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

enum class ChunkStates {
    START,
    FOUND_CHUNK_START,
    FOUND_TOKEN_START,
    FOUND_TOKEN_END,
    FOUND_CHUNK_END,
    END,
};

bool maybe_found_chunk_start(ChunkStates& state, const std::string& char_buffer) {
    if (str_contains(char_buffer, chunk_start_text)) {
        state = ChunkStates::FOUND_CHUNK_START;
        return true;
    }
    return false;
}

bool maybe_found_end(ChunkStates& state, const std::string& content, int idx) {
    if (idx >= content.size() - 2) {
        state = ChunkStates::END;
        return true;
    }
    return false;
}

bool maybe_found_token_start(ChunkStates& state, const std::string& char_buffer) {
    if (char_buffer == start_token) {
        assert(state == ChunkStates::FOUND_CHUNK_START || state == ChunkStates::FOUND_TOKEN_END);
        state = ChunkStates::FOUND_TOKEN_START;
        return true;
    }
    return false;
}

bool maybe_found_token_end(ChunkStates& state, const std::string& char_buffer) {
    if (char_buffer.size() > end_token.size()) {
        std::string to_test = char_buffer.substr(char_buffer.size() - end_token.size());
        if (to_test == end_token) {
            state = ChunkStates::FOUND_CHUNK_END;
            return true;
        }
    }
    return false;
}

void push_chunks(StreamingResponse* streamed, std::string content) {
    int pushes = 0;
    int i = 0;

    std::string char_buffer;
    ChunkStates state = ChunkStates::START;

    std::string data_chunk = "";
    char c;

    for (i = 0; i <= content.size(); ++i) {
        c = content[i];
        char_buffer += c;
        switch (state) {
            case ChunkStates::START:
                if (maybe_found_chunk_start(state, char_buffer)) { char_buffer.clear(); }
                break;
            case ChunkStates::FOUND_CHUNK_START:
                if (maybe_found_token_start(state, char_buffer)) { break; }
                if (maybe_found_end(state, content, i)) {
                    break;
                }
                break;
            case ChunkStates::FOUND_TOKEN_START:
                if (maybe_found_token_end(state, char_buffer)) {
                    char_buffer.pop_back(); // remove the stray \n from end_token
                    streamed->push(std::move(char_buffer));
                    pushes++;
                }
                break;
            case ChunkStates::FOUND_CHUNK_END:
                if (maybe_found_chunk_start(state, char_buffer)) { break; };
                maybe_found_end(state, content, i);
                break;
            case ChunkStates::END:
                if (pushes == 0) {
                    Logger.failed_to_parse_strings.emplace_back(content);
                }
                return;
            default: break;
        }
    }
    if (state != ChunkStates::END) {
        Logger.failed_to_parse_strings.emplace_back(content);
    }
}

json parse_to_json(std::string json_str) {
    json as_json = json::parse(std::move(json_str));
    return as_json;
}
