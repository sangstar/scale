#include "benchmarks/benchmark.hpp"
#include "benchmarks/cola.hpp"
#include "curl.hpp"


int main() {
    const char* uri = "https://datasets-server.huggingface.co/rows?dataset=nyu-mll%2Fglue&config=cola&split=test&offset=0&length=100";
    auto resp = CURLHandler::get(uri);
    auto cola_json =parse_to_json(std::move(resp));
    ColaBenchmark benchmark(std::move(cola_json));



    auto ctx = BenchmarkContext<ColaBenchmark>(benchmark, "https://api.openai.com/v1/completions");

    // TODO: Make this concurrent and atomic, have this wrap some struct that includes the params,
    //       and fetch all the important metrics at the exact time necessary, such as TTFT, end-to-end
    //       latency, etc. Don't let the threads make this worse. Make these exact and don't let any
    //       workers doing processing to detract from the numbers
    //
    // TODO: Afterwards, then implement scoring
    for (int i = 0; i < 5; i++) {
        auto params = benchmark.request_from_dataset_row(0);
        ctx.send_and_add_to_buffer(params);
    }

    return 1;
}
