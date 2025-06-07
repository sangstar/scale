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

Generally very fast, mostly bottlenecked by network-bound I/O, allowing for
client overhead to not muddy benchmark results even at high request concurrency.

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
INFO: Req 1: {"e2e_latency":0.646018,"finish_reason":"length","guessed_correctly":false,"id":"cmpl-Bc1sQ62nWXXB0SNXgQi3aMRBDjc38","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"N/A","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThe witch vanished into the forest.\nAnswer:","text":"\n","ttft":0.644942,"yes_logprob":"-11.340689"}
INFO: Req 2: {"e2e_latency":0.667457,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1sQDGETsqDVFMdhMEG9tfHW3aFH","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"N/A","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nHerman hammered the metal flat.\nAnswer:","text":" Yes","ttft":0.662227,"yes_logprob":"-0.48385972"}
INFO: Req 3: {"e2e_latency":0.669822,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1sQuYqDxUkgLYNVpQVA5R9AfXEz","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"N/A","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThis building got taller and taller.\nAnswer:","text":" Yes","ttft":0.664623,"yes_logprob":"-0.4354237"}
INFO: Req 4: {"e2e_latency":0.671922,"finish_reason":"length","guessed_correctly":false,"id":"cmpl-Bc1sQdfkAyQBDjAmSrEkzgySpYHbT","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"N/A","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThey made him president.\nAnswer:","text":" \n\n","ttft":0.666648,"yes_logprob":"-10.759635"}
... # truncating some of the logged output to not flood the README
INFO: Req 994: {"e2e_latency":0.944359,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1sWWQg38GknGCEnTEIZNx8NuG12","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-0.2301614","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThe ship sank to collect the insurance.\nAnswer:","text":" No","ttft":0.938047,"yes_logprob":"-7.888147"}
INFO: Req 995: {"e2e_latency":1.01331,"finish_reason":"length","guessed_correctly":false,"id":"cmpl-Bc1sWs34AKWbbQIPOVzSQrWQT98tH","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-0.26403093","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThe writers did so believe the boy.\nAnswer:","text":" No","ttft":1.005713,"yes_logprob":"-9.288511"}
INFO: Req 996: {"e2e_latency":1.033611,"finish_reason":"length","guessed_correctly":false,"id":"cmpl-Bc1sWq398PeBYDuC3lbibM2s8zGpr","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-10.108098","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThis drug's testing on oneself is too risky.\nAnswer:","text":" Yes","ttft":1.028615,"yes_logprob":"-10.978629"}
INFO: Req 997: {"e2e_latency":1.359609,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1sWx0A6bsOqWwimmR1fQk9Prcla","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-10.053342","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nJohn tries not to meet Mary.\nAnswer:","text":" Yes","ttft":1.34793,"yes_logprob":"-0.3806907"}
INFO: Req 998: {"e2e_latency":1.462599,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1sXUYwSog96zIaFwxOs4ezt7ZCV","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"N/A","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nWe like our friends and they like their friends, too.\nAnswer:","text":" Yes","ttft":1.459517,"yes_logprob":"-0.33635765"}
INFO: Req 999: {"e2e_latency":1.732885,"finish_reason":"length","guessed_correctly":false,"id":"cmpl-Bc1sWn8PgBN4AwGtw2PFOeSmXwPBr","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-2.7098823","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nMy uncle didn't buy anything for Christmas, but my aunt did it for him and it was bright red.\nAnswer:","text":"\n","ttft":1.699993,"yes_logprob":"-5.0515704"}
INFO: Req 1000: {"e2e_latency":1.364769,"finish_reason":"length","guessed_correctly":true,"id":"cmpl-Bc1sXA3DbwfcjSb46Dnb3Ncs0AHlG","model":"gpt-3.5-turbo-instruct:20230824-v2","no_logprob":"-0.2120897","object":"text_completion","prompt":"Is the following sentence grammatically acceptable?\nThe problem knows easily.\nAnswer:","text":" No","ttft":1.352536,"yes_logprob":"-6.6817226"}
INFO: 1000 requests processed in 7.630504s, 131.053 reqs/sec | Average TTFT: 0.661s | Average End-to-End Latency: 0.672s | Accuracy: 60%
```