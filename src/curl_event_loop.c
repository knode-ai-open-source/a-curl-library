// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/impl/curl_event_priv.h"
#include "a-curl-library/rate_manager.h"
#include "a-curl-library/event_state.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void curl_event_loop_put_state(curl_event_loop_t *loop, const char *key, const char *value) {
    pthread_mutex_lock(&loop->mutex);
    event_state_t *kv = event_state_find(loop->state, key);
    if(kv) {
        if(kv->value)
            aml_free(kv->value);
        if(value) {
            kv->value = aml_strdup(value);
            curl_event_loop_request_t *pending = kv->requests;
            if(pending) {
                pending->request.start_time = macro_now();
                loop->requests_waiting_on_state--;
                while(pending->next_pending) {
                    pending = pending->next_pending;
                    pending->request.start_time = macro_now();
                    loop->requests_waiting_on_state--;
                }
                pending->next_pending = loop->pending_requests;
                loop->pending_requests = kv->requests;
                kv->requests = NULL;
            }
        }
        else
            kv->value = NULL;
    } else {
        kv = (event_state_t *)aml_calloc(1,sizeof(event_state_t));
        kv->key = aml_strdup(key);
        if(value)
            kv->value = aml_strdup(value);
        else
            kv->value = NULL;
        kv->requests = NULL;
        event_state_insert(&loop->state, kv);
    }
    pthread_mutex_unlock(&loop->mutex);
}

// returns first key that is not set
static event_state_t *event_loop_check(curl_event_loop_t *loop, char **keys) {
    while(*keys) {
        event_state_t *kv = event_state_find(loop->state, *keys);
        if(kv && !kv->value) {
            return kv;
        } else if(!kv) {
            kv = (event_state_t *)aml_calloc(1,sizeof(event_state_t));
            kv->key = aml_strdup(*keys);
            kv->value = NULL;
            kv->requests = NULL;
            event_state_insert(&loop->state, kv);
            return kv;
        }
        keys++;
    }
    return NULL;
}


// must be freed by caller
char *curl_event_loop_get_state(curl_event_loop_t *loop, const char *key) {
    char *value = NULL;
    pthread_mutex_lock(&loop->mutex);
    event_state_t *kv = event_state_find(loop->state, key);
    if(kv && kv->value)
        value = aml_strdup(kv->value);
    pthread_mutex_unlock(&loop->mutex);
    return value;
}

curl_event_loop_t *curl_event_loop_init(curl_event_on_loop_t on_loop, void *arg) {
    aml_pool_t *pool = aml_pool_init(16*1024);

    curl_event_loop_t *loop = (curl_event_loop_t *)aml_pool_zalloc(pool, sizeof(curl_event_loop_t));
    if (!loop) {
        fprintf(stderr, "[curl_event_loop_init] Memory allocation failed.\n");
        aml_pool_destroy(pool);
        return NULL;
    }

    loop->pool = pool;
    loop->on_loop = on_loop;
    loop->on_loop_arg = arg;

    loop->queued_requests = NULL;
    loop->inactive_requests = NULL;
    loop->refresh_requests = NULL;
    loop->state = NULL;
    loop->multi_handle = curl_multi_init();
    if (!loop->multi_handle) {
        fprintf(stderr, "[curl_event_loop_init] Failed to create multi_handle.\n");
        aml_pool_destroy(pool);
        return NULL;
    }

    loop->shared_handle = curl_share_init();
    if (!loop->shared_handle) {
        fprintf(stderr, "[curl_event_loop_init] Failed to create shared_handle.\n");
        curl_multi_cleanup(loop->multi_handle);
        aml_pool_destroy(pool);
        return NULL;
    }

    // Example: share DNS cache
    curl_share_setopt(loop->shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);

    // The user can toggle HTTP/3 support if their libcurl has it
    loop->enable_http3 = true;

    loop->queued_requests = NULL;
    loop->num_queued_requests = 0;
    loop->num_multi_requests = 0;
    loop->inactive_requests = NULL;
    loop->num_inactive_requests = 0;

    loop->refresh_requests = NULL;
    loop->num_refresh_requests = 0;

    loop->cancelled_requests = NULL;
    loop->pending_requests = NULL;
    loop->injected_requests = NULL;
    loop->rate_limited_requests = NULL;

    loop->metrics.total_requests = 0;
    loop->metrics.completed_requests = 0;
    loop->metrics.failed_requests = 0;
    loop->metrics.retried_requests = 0;

    // Let’s default to a high concurrency.
    loop->max_concurrent_requests = 1000;

    loop->keep_running = true;
    pthread_mutex_init(&loop->mutex, NULL);

    return loop;
}

// TODO: Why is this here?
void curl_event_loop_inject(curl_event_loop_t *loop, curl_event_request_t *req) {
    if (!loop || !req) return;

    curl_event_loop_request_t *injected_req = (curl_event_loop_request_t *)aml_calloc(1, sizeof(curl_event_loop_request_t));
    if (!injected_req) {
        fprintf(stderr, "[curl_event_loop_inject] Memory allocation failed.\n");
        return;
    }

    // Initialize the injected request
    injected_req->request = *req; // Copy the request data
    injected_req->is_injected = true;
    injected_req->request.loop = loop; // Ensure the loop reference is correct
    injected_req->request.current_retries = 0;

    // Simulate a completed request
    injected_req->request.userdata = req->userdata;
    injected_req->request.userdata_cleanup = req->userdata_cleanup;
    injected_req->request.on_complete = req->on_complete;

    injected_req->easy_handle = NULL; // No actual cURL handle
    injected_req->multi_handle = NULL;

    pthread_mutex_lock(&loop->mutex);
    injected_req->next_pending = loop->injected_requests;
    loop->injected_requests = injected_req;
    pthread_mutex_unlock(&loop->mutex);
}

bool curl_event_loop_cancel(curl_event_loop_t *loop, curl_event_request_t *r) {
    if (!loop || !r) return false;

    // r will point to the request member of curl_event_loop_request_t
    // offset needs adjusted
    curl_event_loop_request_t *req = (curl_event_loop_request_t *)((char *)r - offsetof(curl_event_loop_request_t, request));

    pthread_mutex_lock(&loop->mutex);

    if (req->next_cancelled) {
        pthread_mutex_unlock(&loop->mutex);
        return false; // Already canceled
    }

    req->next_cancelled = loop->cancelled_requests;
    loop->cancelled_requests = req;

    pthread_mutex_unlock(&loop->mutex);
    return true;
}


void curl_event_loop_destroy(curl_event_loop_t *loop) {
    if (!loop) return;

    // First, clean up requests stored in macro_map_t-based containers

    macro_map_t *n = macro_map_first(loop->queued_requests);
    while(n) {
        curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
        macro_map_erase(&loop->queued_requests, n);
        curl_event_request_destroy(req);
        n = macro_map_first(loop->queued_requests);
    }

    n = macro_map_first(loop->inactive_requests);
    while(n) {
        curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
        macro_map_erase(&loop->inactive_requests, n);
        curl_event_request_destroy(req);
        n = macro_map_first(loop->inactive_requests);
    }

    n = macro_map_first(loop->refresh_requests);
    while(n) {
        curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
        macro_map_erase(&loop->refresh_requests, n);
        curl_event_request_destroy(req);
        n = macro_map_first(loop->refresh_requests);
    }

    n = macro_map_first(loop->rate_limited_requests);
    while(n) {
        curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
        macro_map_erase(&loop->rate_limited_requests, n);
        curl_event_request_destroy(req);
        n = macro_map_first(loop->rate_limited_requests);
    }

    n = macro_map_first(loop->state);
    while(n) {
        event_state_t *kv = (event_state_t *)n;
        macro_map_erase(&loop->state, n);
        if(kv->key)
            aml_free(kv->key);
        if(kv->value)
            aml_free(kv->value);
        aml_free(kv);
        n = macro_map_first(loop->state);
    }

    // Now, clean up requests stored in linked lists
    pthread_mutex_lock(&loop->mutex);

    // Cancelled requests
    curl_event_loop_request_t *req = loop->cancelled_requests;
    while(req) {
        curl_event_loop_request_t *next = req->next_cancelled;
        curl_event_request_destroy(req);
        req = next;
    }
    loop->cancelled_requests = NULL;

    // Pending requests (waiting on dependencies)
    req = loop->pending_requests;
    while(req) {
        curl_event_loop_request_t *next = req->next_pending;
        curl_event_request_destroy(req);
        req = next;
    }
    loop->pending_requests = NULL;

    // Injected requests
    req = loop->injected_requests;
    while(req) {
        curl_event_loop_request_t *next = req->next_pending;
        curl_event_request_destroy(req);
        req = next;
    }
    loop->injected_requests = NULL;

    pthread_mutex_unlock(&loop->mutex);

    // Clean up libcurl handles and the mutex
    curl_multi_cleanup(loop->multi_handle);
    curl_share_cleanup(loop->shared_handle);
    pthread_mutex_destroy(&loop->mutex);

    aml_pool_destroy(loop->pool);
}

static bool request_is_rate_limited(curl_event_loop_t *loop, macro_map_t **root, curl_event_loop_request_t *req) {
    if(!req->request.rate_limit)
        return false;
    uint64_t next = rate_manager_can_proceed(req->request.rate_limit, req->request.rate_limit_high_priority);
    if(next == 0)
        return false;

    macro_map_erase(root, &req->node);
    // insert into a rate limited queue with time as key
    req->request.next_retry_at = macro_now() + next;
    curl_event_request_insert(&loop->rate_limited_requests, req);
    return true;
}

static bool request_waiting_on_dependencies(curl_event_loop_t *loop, curl_event_loop_request_t *req) {
    event_state_t *event_state = NULL;
    if(req->request.dependencies) {
        pthread_mutex_lock(&loop->mutex);
        event_state = event_loop_check(loop, req->request.dependencies);
        if(event_state) {
            req->next_pending = event_state->requests;
            event_state->requests = req;
            loop->requests_waiting_on_state++;
        }
        pthread_mutex_unlock(&loop->mutex);
    }
    return event_state != NULL;
}

/**
 * Check if a request is ready to be retried or become active.
 */
static bool request_ready(curl_event_loop_t *loop, const curl_event_loop_request_t *req) {
    if(loop->num_queued_requests >= loop->max_concurrent_requests) {
        return false;
    }
    if(req->request.rate_limit) {
        if(rate_manager_can_proceed(req->request.rate_limit, req->request.rate_limit_high_priority) > 0) {
            return false;
        }
    }
    if(req->request.dependencies) {
        pthread_mutex_lock(&loop->mutex);
        event_state_t *event_state = event_loop_check(loop, req->request.dependencies);
        pthread_mutex_unlock(&loop->mutex);
        if(event_state)
            return false;
    }

    // Check if the current time is greater than or equal to the retry time
    return macro_now() >= req->request.next_retry_at;
}

static void enqueue_request(curl_event_loop_t *loop, curl_event_loop_request_t *req) {
    if(req->request.should_refresh) {
        curl_event_request_insert(&loop->refresh_requests, req);
        loop->num_refresh_requests++;
    } else {
        curl_event_request_insert(&loop->inactive_requests, req);
        loop->num_inactive_requests++;
    }
}

void process_cancelled_and_pending_requests(curl_event_loop_t *loop) {
    pthread_mutex_lock(&loop->mutex);
    curl_event_loop_request_t *cancelled = loop->cancelled_requests;
    loop->cancelled_requests = NULL; // Reset the list for new cancellations

    curl_event_loop_request_t *pending = loop->pending_requests;
    loop->pending_requests = NULL; // Reset the list for new additions
    pthread_mutex_unlock(&loop->mutex);

    while (cancelled) {
        curl_event_loop_request_t *next = cancelled->next_cancelled;

        if (cancelled->multi_handle) {
            macro_map_erase(&loop->queued_requests, (macro_map_t *)cancelled);
            loop->num_queued_requests--;
        } else {
            if(cancelled->request.should_refresh) {
                macro_map_erase(&loop->refresh_requests, (macro_map_t *)cancelled);
                loop->num_refresh_requests--;
            } else {
                macro_map_erase(&loop->inactive_requests, (macro_map_t *)cancelled);
                loop->num_inactive_requests--;
            }
        }
        curl_event_request_destroy(cancelled);
        cancelled = next;
    }

    while (pending) {
        curl_event_loop_request_t *next = pending->next_pending;
        pending->next_pending = NULL;
        if(!request_waiting_on_dependencies(loop, pending)) {
            if (request_ready(loop, pending)) {
                curl_event_loop_request_start(pending);
            } else {
                enqueue_request(loop, pending);
            }
        }
        pending = next;
    }
}

static long calculate_next_timer_expiry(curl_event_loop_t *loop, long max_value) {
    uint64_t current_time, next_time;

    // Get the first request from the inactive map (sorted by `next_retry_at`)
    macro_map_t *first_inactive_node = macro_map_first(loop->inactive_requests);
    macro_map_t *first_refresh_node = macro_map_first(loop->refresh_requests);
    if (!first_inactive_node && !first_refresh_node) {
        return max_value;
    } else if(!first_inactive_node) {
        curl_event_loop_request_t *r = (curl_event_loop_request_t *)first_refresh_node;
        next_time = r->request.next_retry_at;
    } else if(!first_refresh_node) {
        curl_event_loop_request_t *r = (curl_event_loop_request_t *)first_inactive_node;
        next_time = r->request.next_retry_at;
    } else {
        curl_event_loop_request_t *r = (curl_event_loop_request_t *)first_inactive_node;
        if(!request_ready(loop, r)) {
            r = (curl_event_loop_request_t *)first_refresh_node;
            next_time = r->request.next_retry_at;
        }
        else {
            long inactive_time = r->request.next_retry_at;
            r = (curl_event_loop_request_t *)first_refresh_node;
            long refresh_time = r->request.next_retry_at;
            next_time = inactive_time < refresh_time ? inactive_time : refresh_time;
        }
    }
    current_time = macro_now();
    if(next_time < current_time) {
        return 0;
    }
    next_time = next_time - current_time;
    next_time /= 1000000L; // Convert to milliseconds
    return next_time > max_value ? max_value : next_time;
}

static void move_inactive_requests_to_queue(curl_event_loop_t *loop) {
    macro_map_t *n;
    macro_map_t *root = loop->rate_limited_requests;
    loop->rate_limited_requests = NULL;
    uint64_t now = macro_now();
    n = macro_map_first(root);
    while(n) {
        if(now < ((curl_event_loop_request_t *)n)->request.next_retry_at)
            break;
        if(!request_is_rate_limited(loop, &root, (curl_event_loop_request_t *)n)) {
            if(!request_ready(loop, (curl_event_loop_request_t *)n)) {
                break;
            }
            curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
            macro_map_erase(&root, n);
            curl_event_loop_request_start(req);
        }
        n = macro_map_first(root);
    }
    n = macro_map_first(loop->rate_limited_requests);
    while(n) {
        macro_map_erase(&loop->rate_limited_requests, n);
        curl_event_request_insert(&root, (curl_event_loop_request_t *)n);
        n = macro_map_first(loop->rate_limited_requests);
    }
    loop->rate_limited_requests = root;

    n = macro_map_first(loop->refresh_requests);
    while(n) {
        if(!request_is_rate_limited(loop, &loop->refresh_requests, (curl_event_loop_request_t *)n)) {
            if(!request_ready(loop, (curl_event_loop_request_t *)n)) {
                break;
            }
            curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
            macro_map_erase(&loop->refresh_requests, n);
            loop->num_refresh_requests--;
            curl_event_loop_request_start(req);
        } else
            loop->num_refresh_requests--;
        n = macro_map_first(loop->refresh_requests);
    }

    n = macro_map_first(loop->inactive_requests);
    while(n) {
        if(!request_is_rate_limited(loop, &loop->inactive_requests, (curl_event_loop_request_t *)n)) {
            if(!request_ready(loop, (curl_event_loop_request_t *)n)) {
                break;
            }
            curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
            macro_map_erase(&loop->inactive_requests, n);
            loop->num_inactive_requests--;
            curl_event_loop_request_start(req);
        } else
            loop->num_inactive_requests--;
        n = macro_map_first(loop->inactive_requests);
    }
}

static void process_completed_requests(curl_event_loop_t *loop) {
    int msgs_left = 0;
    CURLMsg *msg = NULL;
    while ((msg = curl_multi_info_read(loop->multi_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy = msg->easy_handle;
            curl_event_loop_request_t *req = NULL;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, (void **)&req);

            long http_code = 0;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);
            CURLcode result = msg->data.result;

            // Remove handle from multi
            macro_map_erase(&loop->queued_requests, (macro_map_t *)req);
            loop->num_queued_requests--;

            bool success = result == CURLE_OK && http_code == 200;
            int retry_in;
            if(result == CURLE_OK && http_code == 200) {
                retry_in = req->request.on_complete(easy, &req->request);
            } else {
                retry_in = -1;
                if(req->request.on_failure)
                     retry_in = req->request.on_failure(easy, result, http_code, &req->request);
            }

            // Handle 429: Too Many Requests
            if (http_code == 429 && req->request.rate_limit) {
                retry_in = rate_manager_handle_429(req->request.rate_limit);
                req->request.next_retry_at = macro_now_add_seconds(retry_in);
                curl_event_request_insert(&loop->rate_limited_requests, req);
                continue;
            }

            if (req->request.rate_limit) {
                rate_manager_request_done(req->request.rate_limit);
            }

            if(retry_in > 0) {
                req->request.next_retry_at = macro_now_add_seconds(retry_in);
                curl_event_loop_request_cleanup(req);  // these don't count towards retries
                enqueue_request(loop, req);
            } else if(retry_in < 0 && req->request.on_retry(&req->request)) {
                curl_event_loop_request_cleanup(req);
                loop->metrics.retried_requests++;
                enqueue_request(loop, req);
            } else {
                if(success)
                    loop->metrics.completed_requests++;
                // Clean up resources
                if (req->request.should_refresh) {
                    req->request.current_retries = 0;
                    curl_event_loop_request_cleanup(req);
                    enqueue_request(loop, req);
                } else
                    curl_event_request_destroy(req);
            }
        }
    }

    pthread_mutex_lock(&loop->mutex);
    curl_event_loop_request_t *injected = loop->injected_requests;
    loop->injected_requests = NULL; // Reset the injected queue
    pthread_mutex_unlock(&loop->mutex);

    while (injected) {
        curl_event_loop_request_t *next = injected->next_pending;

        if (injected->request.on_complete) {
            injected->request.on_complete(NULL, &injected->request);
        }

        curl_event_request_destroy(injected);
        injected = next;
    }
}

/**
 * Run the event loop.
 * A simple approach that calls curl_multi_wait + curl_multi_perform in a loop.
 * More advanced usage would integrate with epoll, libuv, etc.
 */
void curl_event_loop_run(curl_event_loop_t *loop) {
    if (!loop) return;

    loop->keep_running = true;

    while (loop->keep_running) {
        // Allow user-defined loop logic (e.g., dynamically enqueue requests)
        if (loop->on_loop && !loop->on_loop(loop, loop->on_loop_arg)) {
            break;
        }

        // Process pending and cancelled requests
        process_cancelled_and_pending_requests(loop);

        // Move ready requests from inactive to active queue
        move_inactive_requests_to_queue(loop);

        // Check if we have active requests in the multi_handle
        int still_running = 0;
        if (loop->num_queued_requests > 0) {
            curl_multi_perform(loop->multi_handle, &still_running);
        }

        // Handle completed requests
        process_completed_requests(loop);

        // Check if we should exit: no running transfers, no pending requests
        if (still_running == 0 &&
            loop->requests_waiting_on_state == 0 &&
            loop->pending_requests == NULL &&
            macro_map_first(loop->queued_requests) == NULL &&
            macro_map_first(loop->refresh_requests) == NULL &&
            macro_map_first(loop->inactive_requests) == NULL) {
            break;
        }

        // Calculate next wait time
        long wait_timeout_ms = calculate_next_timer_expiry(loop, 200);

        // Wait for I/O readiness or timeout
        if (loop->num_multi_requests > 0) {
            int num_fds = 0;
            CURLMcode mc = curl_multi_poll(loop->multi_handle, NULL, 0, wait_timeout_ms, &num_fds);
            if (mc != CURLM_OK) {
                fprintf(stderr, "curl_multi_poll() failed: %s\n", curl_multi_strerror(mc));
            }
        } else if (wait_timeout_ms > 0) {
            usleep(wait_timeout_ms * 1000); // Sleep when idle to prevent CPU spinning
        }
    }
}

void curl_event_loop_stop(curl_event_loop_t *loop) {
    if (!loop) return;
    loop->keep_running = false;
}

curl_event_metrics_t curl_event_loop_get_metrics(const curl_event_loop_t *loop) {
    if (!loop) {
        curl_event_metrics_t empty = {0};
        return empty;
    }
    return loop->metrics;
}
