#include "benchmarks/benchmark.hpp"
#include "benchmarks/cola.hpp"
#include "curl.hpp"
#include "logger.hpp"

const std::string filename = "stdout";
LoggingContext Logger(filename, NOTSET);

int main() {
    DatasetParams params("nyu-mll/glue", "cola", "train");
    params.ms_between_curl = 1000;
    Dataset dataset = params.get_dataset();
    ColaBenchmark benchmark(std::move(dataset));


    Logger.write("Beginning benchmark..");
    auto ctx = BenchmarkContext<ColaBenchmark>(benchmark, "https://api.openai.com/v1/completions");

    auto start = std::chrono::high_resolution_clock::now();
    ctx.perform_benchmark("output.jsonl", &Logger);
    auto end = std::chrono::high_resolution_clock::now();
    auto benchmark_duration = end - start;
    auto seconds = duration_cast<std::chrono::duration<double>>(benchmark_duration).count();
    std::cout << "Took " << seconds << "s";
    return 1;
}
