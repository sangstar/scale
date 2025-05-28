# `scale`

OpenAI API server benchmarker and evaluator

## What it does

`scale` is a benchmarking and evaluation client for OpenAI
API-compatible inference servers written in C++. It does stuff
like:

- Loads datasets for evaluation like from GLUE
- Marshals dataset rows in to OpenAI API server `v1/completions`
  requests to an inference service with arbitrary concurrency
- Records latency metrics like time-to-first-token, end-to-end
  latency and evaluation metrics and writes the results to a file
  or stdout

Generally very fast, mostly bottlenecked by network-bound I/O. Able to 
send, receive, and process over 100 requests per second from the official
OpenAI REST API, making the client bottleneck negligible when benchmarking.

## Usage example:
The arguments to pass to `scale` are:

```shell
./scale --help

Usage: scale [OPTIONS]

Options:
  --base-url <url>     Base URL to fetch from (e.g. https://api.openai.com/v1/completions)
  --id <id>            HuggingFace Dataset identifier (e.g. nyu-mll/glue)
  --config <config>    Config name (e.g. cola)
  --split <split>      Dataset split (e.g. train, test)
  --outfile <path>     Output jsonl file path (e.g. output.jsonl)
  --req-rate <int>     Time between requests to server in ms (default 1000)
  -h, --help           Show this help message
```

And with an example run:
```shell
./scale --base-url https://api.openai.com/v1/completions --id nyu-mll/glue --config cola --split train --outfile results.jsonl
INFO: Fetching data..
INFO: Got 1000 rows.
INFO: Beginning benchmark..
INFO: Req 0: {"e2e_latency":0.835525,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1Bo0WPRLE6hGnkT84VO9fzBKPhD","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"N/A","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThe rope stretched over the pulley.\nAnswer:","text":" Yes","ttft":0.835182,"yes_logprob":"-0.4421997"}
INFO: Req 1: {"e2e_latency":0.831562,"finish_reason":"length","guessed_correctly":false,"id":"cmpl-Bc1BoTcsmb2WEHflH2b6Xp7RAIctH","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-1.0574633","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThe car honked down the road.\nAnswer:","text":"\n","ttft":0.831466,"yes_logprob":"-4.955828"}
INFO: Req 2: {"e2e_latency":0.835154,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1Bop8ywn1DrKYEb2dOfFL9VCVLm","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-12.220061","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nBill sang Sue to sleep.\nAnswer:","text":" Yes","ttft":0.834976,"yes_logprob":"-0.4588707"}
... # truncating some of the logged output to not flood the README
INFO: Req 998: {"e2e_latency":0.957958,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1BvrWBsR6rPbCKObqXhGlciD4Wv","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-10.0712185","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThe moral destruction of the president was certainly not helpful.\nAnswer:","text":" Yes","ttft":0.949041,"yes_logprob":"-0.5319102"}
INFO: Req 999: {"e2e_latency":1.479182,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1BvI2QbAt6NPHyjQpdzW6pXaVMC","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-0.46780014","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nI'll turn the radio down, but I won't up.\nAnswer:","text":" No","ttft":1.477351,"yes_logprob":"-6.915308"}
INFO: 1000 requests processed in 8.093664667s.
```

