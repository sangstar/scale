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
send, receive, and process around 100 requests per second from the official
OpenAI REST API.

