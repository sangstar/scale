#include "benchmarks/benchmark.hpp"
#include "benchmarks/cola.hpp"
#include "curl.hpp"
#include "logger.hpp"

const std::string filename = "stdout";

#ifdef NDEBUG
LoggingContext Logger(filename, INFO);
#else
LoggingContext Logger(filename, DEBUG);
#endif


const std::string_view help_text = R"(
Usage: scale [OPTIONS]

Options:
  --base-url <url>     Base URL to fetch from (e.g. https://api.openai.com/v1/completions)
  --id <id>            HuggingFace Dataset identifier (e.g. nyu-mll/glue)
  --config <config>    Config name (e.g. cola)
  --split <split>      Dataset split (e.g. train, test)
  --outfile <path>     Output jsonl file path (e.g. output.jsonl)
  --req-rate <int>     Time between requests to server in ms (default 1000)
  -h, --help           Show this help message
)";

int main(int argc, char* argv[]) {

    auto check_required_args = [](std::string& to_set, const char* cli_arg) {
        if (to_set.empty()) {
            std::cerr << "Required arg not set: " << cli_arg << std::endl;
            exit(1);
        }
        return to_set;
    };

    std::string base_url;
    std::string id;
    std::string config;
    std::string split;
    std::string outfile;
    std::string req_rate = "1000";

    int req_rate_as_int = 1000;

    std::string arg;
    for (int i = 1; i < argc; ++i) {
        arg = argv[i];
        if (i == 1 && (arg == "--help" || arg == "-h")) {
            std::cout << help_text << std::endl;
            return 1;
        }
        if (arg == "--base-url" && i + 1 < argc) {
            base_url = argv[++i];
        } else if (arg == "--id" && i + 1 < argc) {
            id = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            config = argv[++i];
        } else if (arg == "--split" && i + 1 < argc) {
            split = argv[++i];
        } else if (arg == "--outfile" && i + 1 < argc) {
            outfile = argv[++i];
        } else if (arg == "--req-rate" && i + 1 < argc) {
            req_rate = argv[++i];
            req_rate_as_int = std::stoi(req_rate);
        } else {
            std::cerr << "Unrecognized or incomplete argument: " << arg << "\n";
            return 1;
        }
    }

    check_required_args(base_url, "--base-uri");
    check_required_args(id, "--id");
    check_required_args(config, "--config");
    check_required_args(split, "--split");
    check_required_args(outfile, "--outfile");

    Logger.info("Fetching data..");

    DatasetParams params(id.c_str(), config.c_str(), split.c_str());
    params.ms_between_curl = req_rate_as_int;
    Dataset dataset = params.get_dataset();
    ColaBenchmark benchmark(std::move(dataset));


    Logger.info("Beginning benchmark..");
    auto ctx = BenchmarkContext<ColaBenchmark>(benchmark, base_url.c_str());

    ctx.perform_benchmark(outfile.c_str());
    return 1;
}
