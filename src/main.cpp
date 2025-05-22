#include "benchmarks/benchmark.hpp"
#include "benchmarks/cola.hpp"
#include "curl.hpp"
#include "logger.hpp"

const std::string filename = "stdout";
LoggingContext Logger(filename);

int main() {
    const char* uri = "https://datasets-server.huggingface.co/rows?dataset=nyu-mll%2Fglue&config=cola&split=train&offset=0";
    auto resp = CURLHandler::get(uri);
    auto cola_json =parse_to_json(std::move(resp));
    ColaBenchmark benchmark(std::move(cola_json));


    Logger.write("Beginning benchmark..");
    auto ctx = BenchmarkContext<ColaBenchmark>(benchmark, "https://api.openai.com/v1/completions");

    ctx.perform_benchmark("output.jsonl", &Logger);
    return 1;
}
