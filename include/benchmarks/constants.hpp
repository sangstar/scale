//
// Created by Sanger Steel on 5/23/25.
//

#pragma once
#include <string_view>

namespace WorkerConstants {
    static constexpr int NumWorkersPerRequest = 3;
    static constexpr int NumConcurrentRequests = 100;
}

namespace BenchmarkingConstants {
    static constexpr std::string_view rate_limit_text =
        "        <h1>429</h1>\n"
        "        <p>We had to rate limit you.";
    static constexpr std::string_view format_string = "https://datasets-server.huggingface.co/rows?dataset={}&config={}&split={}&offset={}";
}