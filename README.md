# a-curl-library

High-level, callback-driven abstraction over **libcurl** for orchestrating many concurrent HTTP(S) requests with:

* Central **event loop** (multi-handle based) with pluggable request objects.
* Flexible **retry & backoff** semantics (function return codes drive retry timing).
* Built‑in **rate limiting** (per logical key: max concurrent + requests/sec + backoff on 429).
* Lightweight **worker pool** for off‑loop work.
* Modular **plugins** for common APIs (OpenAI, Google Cloud: Vision, Custom Search, Pub/Sub, GCS, Cloud SQL, Spanner session, Embeddings, etc.).
* Multiple **output interfaces** (memory buffer, file, embeddings collector, chat aggregation, Pub/Sub decoding, custom outputs).
* Simple **state key/value store** for sharing auth tokens, sessions & derived data across requests.
* Clean C API, Apache 2.0 licensed.

> **Status:** Headers only excerpted here; implementations subject to change. Expect C11 + libcurl.

## Table of Contents

1. [Concepts](#concepts)
2. [Request Lifecycle & Callbacks](#request-lifecycle--callbacks)
3. [Retry Semantics](#retry-semantics)
4. [Rate Limiting](#rate-limiting)
5. [Event Loop API](#event-loop-api)
6. [Worker Pool](#worker-pool)
7. [Outputs](#outputs)
8. [Plugins](#plugins)
9. [Quick Start](#quick-start)
10. [Examples](#examples)
11. [Building](#building)
12. [Metrics](#metrics)
13. [License](#license)

## Concepts

**curl\_event\_loop\_t** manages all active requests. Each **curl\_event\_request\_t** describes one HTTP interaction plus policy (timeouts, max retries, backoff, rate limit key, dependencies, callbacks, output interface). A lightweight **state store** lets you inject & retrieve strings (e.g. API tokens) by key. **Outputs** encapsulate response handling (streaming, accumulation, decoding, persistence). **Plugins** are convenience initializers that wrap common service-specific request construction.

## Request Lifecycle & Callbacks

Key callback types (see `curl_event_loop.h`):

* `write_cb` (`curl_event_write_callback_t`) – required; receives body chunks when not using a higher-level output interface (many outputs set this internally via `curl_output_defaults`).
* `on_complete` – required; invoked when request finishes (success or after final processing).
* `on_failure` – optional; given `CURLcode` + HTTP status, decides retry behavior.
* `on_retry` – optional; invoked before scheduling a retry.
* `on_prepare` – optional; invoked just before performing the request (e.g., to refresh headers, inject token from state, modify URL, etc.).
* `userdata_cleanup` – optional destructor for `userdata`.

Timing helpers: `curl_event_request_time_spent`, `curl_event_request_time_spent_on_request`.

## Retry Semantics

Callbacks return an **int** controlling retry:

* `0` → do **not** retry.
* `-1` → retry immediately (subject to rate limits / scheduling).
* `>0` → retry after *n* seconds (supports exponential backoff via stored `backoff_factor`).
  Internal fields `current_retries`, `next_retry_at` manage state. `max_retries = -1` means unlimited; `0` disables retries.

## Rate Limiting

`rate_manager.h` exposes global functions:

* `rate_manager_set_limit(key, max_concurrent, max_rps)` before enqueue.
* Each request can specify `rate_limit` string key and `rate_limit_high_priority` flag (high priority skips queued order within limit constraints).
* 429 handling: `rate_manager_handle_429` returns seconds to wait (exponential backoff). Library updates scheduling accordingly.

## Event Loop API

Core functions:

```c
curl_event_loop_t *curl_event_loop_init(curl_event_on_loop_t on_loop, void *arg);
void curl_event_loop_destroy(curl_event_loop_t *loop);
void curl_event_loop_run(curl_event_loop_t *loop);
void curl_event_loop_stop(curl_event_loop_t *loop);

curl_event_request_t *curl_event_loop_enqueue(curl_event_loop_t *loop,
    const curl_event_request_t *req, int priority);
bool curl_event_loop_cancel(curl_event_loop_t *loop, curl_event_request_t *r);

void curl_event_loop_put_state(curl_event_loop_t *loop, const char *key, const char *value);
char *curl_event_loop_get_state(curl_event_loop_t *loop, const char *key); // caller frees
```

`on_loop` (if provided) is executed each iteration; return `false` to terminate early.

**Headers & Dependencies:** You may define `dependencies` on a request (array of state keys). The request will not execute until each key has a valid (non-NULL) value. This is useful for auth token fetch → dependent API call chains.

## Worker Pool

`worker_pool.h` offers a simple background pool:

```c
worker_pool_t *worker_pool_init(int num_threads);
void worker_pool_push(worker_pool_t *pool, void (*func)(void *), void *arg);
void worker_pool_destroy(worker_pool_t *pool);
```

Use for CPU-bound or blocking work you do *after* receiving responses without stalling the event loop.

## Outputs

Output interfaces wrap response handling:

* **Memory:** `memory_output` collects body into RAM then invokes `memory_complete_callback_t`.
* **File:** `file_output` streams directly to disk + optional completion callback.
* **OpenAI Chat:** `openai_chat_output` aggregates assistant output, token counts.
* **Embeddings:** `openai_embed_output`, `google_embed_output` accumulate float vectors and invoke `embedding_complete_callback_t` with a 2D array.
* **Pub/Sub:** `pubsub_output` decodes JSON payloads, attributes, manages ack/nack flows (optionally pre-ack) and has per-message + completion callbacks.
* **Custom:** Implement your own `curl_output_interface_t` (init, write, failure, complete, destroy). Use `curl_output_defaults(req, output)` to wire defaults.

## Plugins

Convenience wrappers that build & enqueue configured requests (all take an existing loop):

* **Auth / Tokens:** `curl_event_plugin_gcloud_token_init` (service account JWT → access token, optional auto-refresh via `should_refresh`).
* **Embeddings:** `curl_event_plugin_openai_embed_init`, `curl_event_plugin_google_embed_init`.
* **Chat:** `curl_event_plugin_openai_chat_init` (model id, temperature, max tokens, messages array, optional delay ms).
* **Google Vision:** `curl_event_plugin_google_vision_init` (web detection) + `..._set_rate()`.
* **Custom Search:** `curl_event_plugin_google_custom_search_init` + `..._set_rate()`.
* **GCS Download:** `curl_event_plugin_gcs_download_init` (optionally limit via `max_download_size`).
* **Pub/Sub:** `curl_event_plugin_pubsub_pull_init`, `..._ack_init`, seek functions for timestamp or snapshot.
* **Cloud SQL Query:** `curl_event_plugin_cloudsql_query_init`.
* **Spanner:** `curl_event_plugin_spanner_session_init` (session create), `curl_event_plugin_spanner_query_init` (experimental; REST limitations noted in header comment).

Each plugin typically expects state keys (e.g., OAuth token, API key, session id) prepared via earlier requests or manual `curl_event_loop_put_state` calls.

## Quick Start

```c
#include <curl/curl.h>
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/outputs/memory.h"

static size_t write_in_memory(void *contents, size_t size, size_t nmemb, curl_event_request_t *req) {
    // (If using memory_output you usually do not set write_cb manually.)
    return size * nmemb;
}

static int on_complete(CURL *easy, curl_event_request_t *req) {
    (void)easy; (void)req; // inspect userdata / output state here
    return 0; // no retry
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    curl_event_request_t r = {0};
    r.url = "https://example.com";
    r.method = "GET";
    r.write_cb = write_in_memory; // or use an output interface
    r.on_complete = on_complete;
    r.max_retries = 3;

    curl_event_loop_enqueue(loop, &r, 0);
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
    curl_global_cleanup();
}
```

## Examples

### OpenAI Chat

```c
curl_event_loop_put_state(loop, "openai_api_key", OPENAI_KEY);

curl_output_interface_t *out = openai_chat_output(chat_done_cb, ctx);
bool ok = curl_event_plugin_openai_chat_init(loop,
    "openai_api_key", "gpt-4o", 0.7f, 512,
    messages_array, num_messages, 0, out);
```

### Embeddings

```c
curl_event_loop_put_state(loop, "openai_api_key", OPENAI_KEY);
const char *texts[] = {"hello", "world"};
curl_output_interface_t *embed_out = openai_embed_output(1536, embed_cb, ctx);
curl_event_plugin_openai_embed_init(loop, "openai_api_key", "text-embedding-3-large",
    1536, texts, 2, embed_out);
```

### Pub/Sub Pull

```c
curl_event_loop_put_state(loop, "gcp_token", TOKEN);

curl_output_interface_t *ps_out = pubsub_output(loop, project, subscription,
    "gcp_token", handle_message, ctx, all_done, ctx2, true);

curl_event_plugin_pubsub_pull_init(loop, project, subscription, "gcp_token", 10, ps_out);
```

### GCS Download to File

```c
curl_output_interface_t *fout = file_output("object.bin", file_done_cb, ctx);
curl_event_plugin_gcs_download_init(loop, bucket, object, "gcp_token", fout, -1);
```

## Building

**Dependencies:** libcurl; headers reference sibling libraries: `the-macro-library`, `a-json-library`, `a-memory-library`. Provide include paths & link flags accordingly.

Typical compile (example):

```sh
cc -std=c11 -O2 -Iinclude -lcurl your_app.c -o your_app
```

## Metrics

`curl_event_loop_get_metrics` returns `curl_event_metrics_t` with counters: total, completed, failed, retried. Use after loop run or periodically (thread-safe access considerations depend on implementation).

## Best Practices

* Set rate limits *before* enqueueing requests using `rate_manager_set_limit` with the same `rate_limit` key assigned to requests.
* Use dependencies to serialize auth flows: enqueue token refresh + dependent requests simultaneously; dependents wait automatically.
* Provide `userdata_cleanup` to avoid leaks.
* For long-running loops that continuously enqueue new work, implement `on_loop` to inject requests and return `true` until external stop condition.
* Use `curl_event_loop_stop` from another thread or signal handler to gracefully exit.

## License

Apache-2.0 © 2024-2025 Knode.ai. See SPDX headers in source.

---

*Maintainer: Andy Curtis [contactandyc@gmail.com](mailto:contactandyc@gmail.com)*
