// SPDX-FileCopyrightText: 2024‑2025 Knode.ai
// SPDX-License-Identifier: Apache‑2.0
//
// ░░  INTERNAL HEADER – do NOT install or include from user code  ░░
#ifndef A_CURL_LIBRARY_IMPL_CURL_EVENT_PRIV_H
#define A_CURL_LIBRARY_IMPL_CURL_EVENT_PRIV_H

/*  public facade (defines curl_event_metrics_t, curl_event_on_loop_t, …) */
#include "a-curl-library/curl_event_loop.h"

/*  3‑rd‑party helpers we always need                                  */
#include <pthread.h>
#include <string.h>
#include "the-macro-library/macro_map.h"
#include "the-macro-library/macro_time.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"

/* ------------------------------------------------------------------ */
/* Forward decl for per‑request wrapper ------------------------------ */
typedef struct curl_event_loop_request_s curl_event_loop_request_t;

/* ------------------------------------------------------------------ */
/* Per‑request wrapper that lives in the loop’s containers ----------- */
struct curl_event_loop_request_s {
    macro_map_t node;               /* multimap key = next_retry_at        */
    curl_event_request_t request;   /* public view                         */

    /* libcurl plumbing */
    CURLM *multi_handle;
    CURL  *easy_handle;

    /* book‑keeping */
    struct curl_event_loop_request_s *next_cancelled;
    struct curl_event_loop_request_s *next_pending;

    long  content_length;
    bool  content_length_found;
    bool  is_injected;
};

/*  multimap helper (order by “retry at” timestamp) */
static inline int curl_event_request_compare(const curl_event_loop_request_t *a,
                                             const curl_event_loop_request_t *b)
{
    if (a->request.next_retry_at < b->request.next_retry_at) return -1;
    return a->request.next_retry_at > b->request.next_retry_at;
}
static inline macro_multimap_insert(
        curl_event_request_insert,
        curl_event_loop_request_t,
        curl_event_request_compare);

/* ------------------------------------------------------------------ */
/* Key‑value “state” entries used for dependency resolution ---------- */
typedef struct event_state_s {
    macro_map_t node;
    char *key;
    char *value;                       /* NULL ⇒ unresolved               */
    curl_event_loop_request_t *requests; /* FIFO of waiting requests      */
} event_state_t;

static inline int estate_cmp      (const event_state_t *a,
                                    const event_state_t *b){return strcmp(a->key,b->key);}
static inline int estate_cmp_str  (const char *a,
                                    const event_state_t *b){return strcmp(a,b->key);}

static inline macro_map_insert (event_state_insert, event_state_t, estate_cmp);
static inline macro_map_find_kv(event_state_find , char          ,
                                event_state_t    , estate_cmp_str);

/* ------------------------------------------------------------------ */
/* Full loop object (opaque to users) -------------------------------- */
struct curl_event_loop_s {
    /* user hook */
    curl_event_on_loop_t on_loop;
    void                *on_loop_arg;

    /* allocator */
    aml_pool_t *pool;

    /* libcurl handles */
    CURLM  *multi_handle;
    CURLSH *shared_handle;

    /* flags, limits */
    bool    enable_http3;
    size_t  max_concurrent_requests;
    bool    keep_running;

    /* request containers */
    macro_map_t *queued_requests;
    macro_map_t *inactive_requests;
    macro_map_t *refresh_requests;
    macro_map_t *rate_limited_requests;
    macro_map_t *state;

    int  num_queued_requests;
    int  num_multi_requests;
    int  num_inactive_requests;
    int  num_refresh_requests;
    int  requests_waiting_on_state;

    /* statistics */
    curl_event_metrics_t metrics;

    /* x‑thread lists (protected by mutex) */
    pthread_mutex_t          mutex;
    curl_event_loop_request_t *cancelled_requests;
    curl_event_loop_request_t *pending_requests;
    curl_event_loop_request_t *injected_requests;
};

void  curl_event_loop_request_cleanup(struct curl_event_loop_request_s *req);
void  curl_event_request_destroy      (struct curl_event_loop_request_s *req);
bool  curl_event_loop_request_start   (struct curl_event_loop_request_s *req);

#endif /* A_CURL_LIBRARY_IMPL_CURL_EVENT_PRIV_H */
