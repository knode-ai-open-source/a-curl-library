// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
#ifndef CURL_EVENT_LOOP_H
#define CURL_EVENT_LOOP_H

#include <stdbool.h>
#include <stdint.h>
#include "a-curl-library/curl_event_request.h"  /* brings in callbacks etc. */
#include "a-curl-library/event_state.h"

/* Forward declaration kept for completeness */
struct curl_event_loop_s;
typedef struct curl_event_loop_s curl_event_loop_t;

/* Per‑iteration callback: return false to stop the loop */
typedef bool (*curl_event_on_loop_t)(curl_event_loop_t *loop, void *arg);

/* --------------------------------------------------------------------- */
/* Metrics                                                               */
typedef struct {
    uint64_t total_requests;
    uint64_t completed_requests;
    uint64_t failed_requests;
    uint64_t retried_requests;
} curl_event_metrics_t;

/* --------------------------------------------------------------------- */
/* Core loop control API                                                 */
curl_event_loop_t *curl_event_loop_init(curl_event_on_loop_t on_loop, void *arg);
void               curl_event_loop_destroy(curl_event_loop_t *loop);

curl_event_request_t *curl_event_loop_enqueue(curl_event_loop_t       *loop,
                                              const curl_event_request_t *req,
                                              int priority);
bool  curl_event_loop_cancel(curl_event_loop_t *loop, curl_event_request_t *r);

void  curl_event_loop_run (curl_event_loop_t *loop);
void  curl_event_loop_stop(curl_event_loop_t *loop);

curl_event_metrics_t curl_event_loop_get_metrics(const curl_event_loop_t *loop);

#endif /* CURL_EVENT_LOOP_H */
