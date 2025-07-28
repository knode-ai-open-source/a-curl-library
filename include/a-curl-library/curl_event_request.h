// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
#ifndef CURL_EVENT_REQUEST_H
#define CURL_EVENT_REQUEST_H

#include <curl/curl.h>
#include <stdint.h>
#include <stdbool.h>
#include "a-curl-library/curl_output.h"

/* Forward declaration of the loop */
struct curl_event_loop_s;
typedef struct curl_event_loop_s curl_event_loop_t;

/* --------------------------------------------------------------------- */
/* Callback typedefs that act on a single request                        */
typedef int    (*curl_event_on_complete_t)(CURL *easy,
                                           struct curl_event_request_s *req);
typedef int    (*curl_event_on_failure_t)(CURL *easy, CURLcode res, long http,
                                          struct curl_event_request_s *req);
typedef size_t (*curl_event_write_callback_t)(void *ptr, size_t size,
                                              size_t nmemb,
                                              struct curl_event_request_s *req);
typedef bool   (*curl_event_on_retry_t)  (struct curl_event_request_s *req);
typedef bool   (*curl_event_on_prepare_t)(struct curl_event_request_s *req);
typedef void   (*curl_event_cleanup_userdata_t)(void *userdata);

/* --------------------------------------------------------------------- */
/* Public request descriptor                                             */
typedef struct curl_event_request_s {
    /*– ownership & wiring –*/
    curl_event_loop_t *loop;

    /*– basic HTTP info –*/
    char *url;
    char *method;                 /* default "GET" unless post_data → "PUT"  */
    char *post_data;
    struct curl_slist *headers;

    /*– dependency / throttling –*/
    char   **dependencies;        /* NULL‑terminated array                     */
    char   *rate_limit;
    bool    rate_limit_high_priority;

    /*– timeouts / speed –*/
    long connect_timeout;
    long transfer_timeout;
    long low_speed_limit;
    long low_speed_time;

    /*– retry behaviour –*/
    int   max_retries;            /* −1 = unlimited                           */
    double backoff_factor;        /* 0 → default 2.0                          */

    /*– callbacks –*/
    curl_event_on_complete_t    on_complete;   /* required */
    curl_event_on_failure_t     on_failure;    /* optional */
    curl_event_write_callback_t write_cb;      /* required */
    curl_event_on_retry_t       on_retry;      /* optional */
    curl_event_on_prepare_t     on_prepare;    /* optional */

    /*– user payload –*/
    void *userdata;
    curl_event_cleanup_userdata_t userdata_cleanup;

    /*– misc flags –*/
    bool should_refresh;
    bool output_initialized;
    long max_download_size;       /* honoured only if server sends C‑Len */

    /*– internal bookkeeping –*/
    int       current_retries;
    uint64_t  next_retry_at;
    uint64_t  start_time;
    uint64_t  request_start_time;
} curl_event_request_t;

/* Simple timing helpers ------------------------------------------------ */
double curl_event_request_time_spent(const curl_event_request_t *r);
double curl_event_request_time_spent_on_request(const curl_event_request_t *r);

/* Helpers closely tied to a single request ----------------------------- */
long  curl_event_request_content_length(curl_event_request_t *r); /* −1 if unknown */
void  curl_event_loop_update_header(curl_event_request_t *req,
                                    const char *name,
                                    const char *value);

#endif /* CURL_EVENT_REQUEST_H */
