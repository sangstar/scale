//
// Created by Sanger Steel on 5/23/25.
//

#include <catch2/catch_test_macros.hpp>

#include "benchmark_types.hpp"
#include "curl.hpp"
#include "logger.hpp"
#include "constants.hpp"

const std::string filename = "stdout";
LoggingContext Logger(filename, DEBUG);


// TEST_CASE( "CURLHandler can build full dataset" ) {
//     int expected_rows = 3800;
//     DatasetParams params("nyu-mll/glue", "cola", "train");
//     params.ms_between_curl = 1000;
//     Dataset dataset = params.get_dataset();
//     REQUIRE(dataset.rows.size() == expected_rows);
// }
//
// TEST_CASE( "string contains 429" ) {
//     std::string test_str = "<!DOCTYPE html>\n"
// "<html class=\"\" lang=\"en\">\n"
// "<head>\n"
// "    <meta charset=\"utf-8\" />\n"
// "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\" />\n"
// "    <meta name=\"description\" content=\"We're on a journey to advance and democratize artificial intelligence through open source and open science.\" />\n"
// "    <meta property=\"fb:app_id\" content=\"1321688464574422\" />\n"
// "    <meta name=\"twitter:card\" content=\"summary_large_image\" />\n"
// "    <meta name=\"twitter:site\" content=\"@huggingface\" />\n"
// "    <meta property=\"og:title\" content=\"Hugging Face - The AI community building the future.\" />\n"
// "    <meta property=\"og:type\" content=\"website\" />\n"
// "    <title>Hugging Face - The AI community building the future.</title>\n"
// "    <style>\n"
// "        body {\n"
// "            margin: 0;\n"
// "        }\n"
// "        main {\n"
// "            background-color: white;\n"
// "            min-height: 100vh;\n"
// "            padding: 7rem 1rem 8rem 1rem;\n"
// "            text-align: center;\n"
// "            font-family: Source Sans Pro, ui-sans-serif, system-ui, -apple-system,\n"
// "            BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, Noto Sans,\n"
// "            sans-serif, Apple Color Emoji, Segoe UI Emoji, Segoe UI Symbol,\n"
// "            Noto Color Emoji;\n"
// "        }\n"
// "        img {\n"
// "            width: 6rem;\n"
// "            height: 6rem;\n"
// "            margin: 0 auto 1rem;\n"
// "        }\n"
// "        h1 {\n"
// "            font-size: 3.75rem;\n"
// "            line-height: 1;\n"
// "            color: rgba(31, 41, 55, 1);\n"
// "            font-weight: 700;\n"
// "            box-sizing: border-box;\n"
// "            margin: 0 auto;\n"
// "        }\n"
// "        p, a {\n"
// "            color: rgba(107, 114, 128, 1);\n"
// "            font-size: 1.125rem;\n"
// "            line-height: 1.75rem;\n"
// "            max-width: 28rem;\n"
// "            box-sizing: border-box;\n"
// "            margin: 0 auto;\n"
// "        }\n"
// "        .dark main {\n"
// "            background-color: rgb(11, 15, 25);\n"
// "        }\n"
// "        .dark h1 {\n"
// "            color: rgb(209, 213, 219);\n"
// "        }\n"
// "        .dark p, .dark a {\n"
// "            color: rgb(156, 163, 175);\n"
// "        }\n"
// "    </style>\n"
// "    <script>\n"
// "        const key = \"_tb_global_settings\";\n"
// "        let theme = window.matchMedia(\"(prefers-color-scheme: dark)\").matches\n"
// "            ? \"dark\"\n"
// "            : \"light\";\n"
// "        try {\n"
// "            const storageTheme = JSON.parse(window.localStorage.getItem(key)).theme;\n"
// "            if (storageTheme) {\n"
// "                theme = storageTheme === \"dark\" ? \"dark\" : \"light\";\n"
// "            }\n"
// "        } catch (e) {}\n"
// "        if (theme === \"dark\") {\n"
// "            document.documentElement.classList.add(\"dark\");\n"
// "        } else {\n"
// "            document.documentElement.classList.remove(\"dark\");\n"
// "        }\n"
// "    </script>\n"
// "</head>\n"
// "<body>\n"
// "<main>\n"
// "    <img src=\"https://cdn-media.huggingface.co/assets/huggingface_logo.svg\" alt=\"\" />\n"
// "    <div>\n"
// "        <h1>429</h1>\n"
// "        <p>We had to rate limit you. If you think it's an error, send us <a href=\"mailto:website@huggingface.co\">an email</a></p>\n"
// "    </div>\n"
// "</main>\n"
// "</body>\n"
// "</html>\n";
//     REQUIRE(str_contains(test_str, BenchmarkingConstants::rate_limit_text.data()));
// }
//
// TEST_CASE( "Perform benchmark with thread monitor" ) {
//
//     DatasetParams params("nyu-mll/glue", "cola", "train");
//     params.ms_between_curl = 1000;
//     Logger.info("Getting dataset...");
//     Dataset dataset = params.get_dataset();
//     ColaBenchmark benchmark(dataset.rows);
//
//
//     Logger.info("Beginning benchmark..");
//     auto ctx = BenchmarkContext<ColaBenchmark>(benchmark, "https://api.openai.com/v1/completions");
//
//     // Monitor monitor(ctx);
//     ctx.perform_benchmark("output.jsonl");
// }

TEST_CASE("Test parse JSON") {
    //std::string test_str = "data: {\"id":\"cmpl-BhTOHw4rgxTfgXoZn4NFNgyZGfUPr\",\"object\":\"text_completion\",\"created\":1749700781,"choices":[{\"text\":\"no\",\"index\":0,\"logprobs\":{\"tokens\":[\"no\"],\"token_logprobs\":[-0.67217714],\"top_logprobs\":[{\"no\":-0.67217714,\"No\":-1.3748653,"\n":-1.828057," no":-3.2456062," No":-3.6321113,"\n\n":-4.7127924," \n":-7.6328516,"Yes":-8.465514,"yes":-8.813094," \n\n":-9.108312,"NO":-9.468816," ":-9.611159,"<|endoftext|>":-10.157262,"\tno":-10.272944," yes":-10.533286," Yes":-10.560655,"N":-11.056375,"n":-11.181727,"\n \n":-11.410088,"Not":-11.467097}],"text_offset":[215]},"finish_reason":"length"}],"model":"gpt-3.5-turbo-instruct:20230824-v2"}
}
