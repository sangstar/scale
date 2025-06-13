//
// Created by Sanger Steel on 6/6/25.
//

#include "benchmark_types.hpp"
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
  <path-to-yaml>         Path to a .yaml file containing the benchmark config

  --base-url <url>       Base URL to fetch from (e.g. https://api.openai.com/v1/completions)
  --outfile <path>       Output jsonl file path (e.g. output.jsonl)
  --concurrency <int>    Number of concurrent requests to send to the server (default 100)
  --n-samples <int>      Maximum number of samples (default 10000)
  --timeout <int>        Maximum seconds to wait before retrying a request (default no timeout)
  --help                 Show this help message
)";


int main(int argc, char* argv[]) {
    auto check_required_args = [](std::string& to_set, const char* cli_arg) {
        if (to_set.empty()) {
            std::cerr << "Required arg not set: " << cli_arg << std::endl;
            exit(1);
        }
        return to_set;
    };

    signal(SIGABRT, [](int) {
        void* trace[64];
        int n = backtrace(trace, 64);
        backtrace_symbols_fd(trace, n, STDERR_FILENO);
        _exit(1);
    });

    std::string config_path_or_help;

    std::string base_url;
    std::string outfile;
    std::optional<std::string> concurrency = std::nullopt;
    std::optional<std::string> n_samples = std::nullopt;
    std::optional<std::string> timeout_sec = std::nullopt;

    config_path_or_help = argv[1];

    if (config_path_or_help == "--help") {
        std::cout << help_text << std::endl;
        return 1;
    }

    std::string arg;
    for (int i = 2; i < argc; ++i) {
        arg = argv[i];
        if (arg == "--base-url" && i + 1 < argc) {
            base_url = argv[++i];
        } else if (arg == "--outfile" && i + 1 < argc) {
            outfile = argv[++i];
        } else if (arg == "--timeout" && i + 1 < argc) {
            timeout_sec = argv[++i];
        } else if (arg == "--n-samples" && i + 1 < argc) {
            n_samples = argv[++i];
        } else if (arg == "--concurrency" && i + 1 < argc) {
            concurrency = argv[++i];
        } else {
            std::cerr << "Unrecognized or incomplete argument: " << arg << "\n";
            return 1;
        }
    }

    check_required_args(config_path_or_help, "<path-to-yaml>");
    check_required_args(base_url, "--base-uri");
    check_required_args(outfile, "--outfile");

    std::optional<long> timeout_long = std::nullopt;
    if (timeout_sec.has_value()) {
        timeout_long = std::stol(timeout_sec.value());
    }

    int concurrent_requests = 100;
    if (concurrency.has_value()) {
        auto& concurrency_as_str = concurrency.value();
        Logger.debug("Using {} concurrent requests", concurrency_as_str);
        concurrent_requests = std::stoi(concurrency_as_str);
    }


    Logger.info("Fetching data..");

    Dataset params = std::make_unique<HFDatasetParser>(config_path_or_help.c_str());
    if (n_samples.has_value()) {
        auto samples = n_samples.value();
        Logger.debug("Max samples: {}", samples);
        params->max_rows = std::stoi(samples);
    }
    params->download();


    auto shared_client = std::make_shared<CURLHandler>("https://api.openai.com/v1/completions",
                                                       std::getenv("OPENAI_API_KEY"),
                                                       timeout_long);

    DatasetToRequestStrategy dataset_processor(std::move(params));

    FileWritingStrategy writer;
    RequestTransportStrategy sender_and_parser;

    ProcessingStrategy processor{
        dataset_processor,
        sender_and_parser,
        writer,
        shared_client,
        concurrent_requests
    };

    auto result = processor.process_benchmark("output_new.jsonl");
    return 0;
}
