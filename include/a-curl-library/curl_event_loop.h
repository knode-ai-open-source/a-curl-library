// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef CURL_EVENT_LOOP_H
#define CURL_EVENT_LOOP_H

#include <curl/curl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "the-macro-library/macro_map.h"

// Forward declaration
struct curl_event_loop_s;
typedef struct curl_event_loop_s curl_event_loop_t;

struct curl_event_request_s;
typedef struct curl_event_request_s curl_event_request_t;

/**
 * Callback invoked when a request completes or fails.
 * @param easy_handle The CURL easy handle for the request.
 * @param result CURLcode result (e.g., CURLE_OK if success).
 * @param http_code The HTTP status code if the request completed enough
 *                  to receive a status, or 0 if not available.
 * @param userdata Pointer to user data set in the request.
 *
 * return 0 to not retry, -1 to retry, + to retry in n seconds
 */
typedef int (*curl_event_on_complete_t)(
    CURL *easy_handle,
    curl_event_request_t *req
);

/** return 0 to not retry, -1 to retry, + to retry in n seconds */
typedef int (*curl_event_on_failure_t)(
    CURL *easy_handle,
    CURLcode result,
    long http_code,
    curl_event_request_t *req
);

/**
 * Callback for writing response data in-memory (alternative to writing to file).
 * @param contents Pointer to the data buffer.
 * @param size Size of one data item.
 * @param nmemb Number of data items.
 * @param userdata Pointer to user data set in the request.
 * @return Number of bytes actually taken care of (usually size*nmemb).
 */
typedef size_t (*curl_event_write_callback_t)(
    void *contents,
    size_t size,
    size_t nmemb,
    curl_event_request_t *req
);

/* This is only defined here, but meant to be used in the plugins */
typedef struct curl_output_interface_s {
    bool (*init)(struct curl_output_interface_s *userdata, long content_length);
    size_t (*write)(const void *data, size_t size, size_t nmemb, struct curl_output_interface_s *userdata);
    void (*failure)(CURLcode result, long http_code,
                    struct curl_output_interface_s *userdata, curl_event_request_t *req);
    void (*complete)(struct curl_output_interface_s *userdata, curl_event_request_t *req);
    void (*destroy)(struct curl_output_interface_s *userdata);
} curl_output_interface_t;

void curl_output_defaults(curl_event_request_t *req, curl_output_interface_t *output);

typedef void (*curl_event_cleanup_userdata_t)(void *userdata);
typedef bool (*curl_event_on_retry_t)(curl_event_request_t *req);
typedef bool (*curl_event_on_prepare_t)(curl_event_request_t *req);

// return false to break the loop
typedef bool (*curl_event_on_loop_t)(curl_event_loop_t *loop, void *arg);

/**
 * Structure describing a single request.
 */
struct curl_event_request_s {
    curl_event_loop_t *loop;

    char *url;
    char *method;    // e.g. "GET", "POST", "PUT"
    char *post_data; // For POST/PUT requests
    struct curl_slist *headers;

    // null terminated array of dependencies (state keys that must have a valid value), NULL if none
    char **dependencies;

    // To use a rate limit, it must be set via rate_manager_set_limit(...) before the request is enqueued.
    char *rate_limit;
    // if there is a rate limit, high priority requests skip the queue
    bool rate_limit_high_priority;

    long connect_timeout;
    long transfer_timeout;
    long low_speed_limit;
    long low_speed_time;

    // -1 means no limit, 0 means no retries
    int max_retries;

    curl_event_on_complete_t on_complete; // must not be NULL
    curl_event_on_failure_t on_failure;   // may be NULL
    curl_event_write_callback_t write_cb; // must not be NULL
    curl_event_on_retry_t on_retry;
    curl_event_on_prepare_t on_prepare;
    curl_event_cleanup_userdata_t userdata_cleanup;
    void *userdata;

    bool should_refresh; // Should the request persist after success?
    double backoff_factor; // base factor for exponential backoff (defaults to 2.0 if 0.0)

    bool output_initialized;

    long max_download_size; // Maximum download size to download (only works with content-length header)

    // generally not set by the user (unless on_retry/on_refresh is defined)
    int current_retries;
    uint64_t next_retry_at;
    uint64_t start_time;
    uint64_t request_start_time;
};

// return the time in seconds spent on the request
double curl_event_request_time_spent(curl_event_request_t *r);
double curl_event_request_time_spent_on_request(curl_event_request_t *r);


/**
 * A metrics struct to track some basic stats about the event loop
 */
typedef struct {
    uint64_t total_requests;
    uint64_t completed_requests;
    uint64_t failed_requests;
    uint64_t retried_requests;
} curl_event_metrics_t;

/**
 * Initialize the event loop.
 * Note: You must call curl_global_init(...) before this.
 * on_loop will be called after each iteration of the event loop and can be used to enqueue new requests or
 * perform other actions.
 */
curl_event_loop_t *curl_event_loop_init(curl_event_on_loop_t on_loop, void *arg);

/**
 * Destroy the event loop.
 * Note: After calling this, the loop is no longer usable.
 */
void curl_event_loop_destroy(curl_event_loop_t *loop);

/* Get the content length of the request or -1 if unable to determine. */
long curl_event_request_content_length(curl_event_request_t *r);

/* Update or insert a header in the request */
void curl_event_loop_update_header(curl_event_request_t *req, const char *header_name, const char *new_value);

/* Manually set the state of a key in the event loop (ex. OpenAI key) */
void curl_event_loop_put_state(curl_event_loop_t *loop, const char *key, const char *value);

/* Get the value of a key in the state of the event loop, must be free(d) */
char *curl_event_loop_get_state(curl_event_loop_t *loop, const char *key);

/**
 * Enqueue a request into the event loop.
 * The function makes a copy of the essential fields so the request struct
 * doesn't need to live after calling this.
 * priority acts as a boost to the next_retry, the priority subtracts priority * 1ms from the next_retry
 * A negative priority will cause the request to be retried in the future.
 */
curl_event_request_t *curl_event_loop_enqueue(curl_event_loop_t *loop, const curl_event_request_t *req, int priority);

bool curl_event_loop_cancel(curl_event_loop_t *loop, curl_event_request_t *r);


/**
 * Run the event loop. This function will block until all requests are done
 * or until curl_event_loop_stop(loop) is called from another thread.
 */
void curl_event_loop_run(curl_event_loop_t *loop);

/**
 * Signal the loop to stop as soon as possible.
 */
void curl_event_loop_stop(curl_event_loop_t *loop);

/**
 * Retrieve the internal metrics.
 */
curl_event_metrics_t curl_event_loop_get_metrics(const curl_event_loop_t *loop);

#endif // CURL_EVENT_LOOP_H

