#include "a-curl-library/impl/curl_event_priv.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_output.h"
#include "a-curl-library/rate_manager.h"
#include "the-macro-library/macro_time.h"
#include "a-memory-library/aml_alloc.h"
#include <curl/curl.h>
#include <string.h>
#include <math.h>

/**
Everything that operates on one request:
• curl_event_loop_request_cleanup
• curl_event_request_destroy
• header_callback, curl_event_request_content_length
• setup_curl_handle, curl_event_loop_request_start
• enqueue/clone logic (curl_event_loop_enqueue)
• retry helpers (calculate_next_retry, default_calculate_retry)
• curl_event_request_time_spent*
• curl_event_loop_update_header
**/

static bool default_calculate_retry(curl_event_request_t *req);

void curl_event_loop_request_cleanup(curl_event_loop_request_t *req) {
    if (req->easy_handle) {
        if(req->multi_handle) {
            curl_multi_remove_handle(req->multi_handle, req->easy_handle);
            req->request.loop->num_multi_requests--;
        }
        curl_easy_cleanup(req->easy_handle);
        req->easy_handle = NULL;
        req->multi_handle = NULL;
    }
    req->content_length_found = false;
    req->content_length = -1;
}

void curl_event_request_destroy(curl_event_loop_request_t *req) {
    if (!req) return;
    curl_event_loop_request_cleanup(req);
    if (!req->is_injected) {
        if(req->request.headers)
            curl_slist_free_all(req->request.headers);
        if(req->request.url)
            aml_free(req->request.url);
        if(req->request.rate_limit)
            aml_free(req->request.rate_limit);
        if(req->request.method)
            aml_free(req->request.method);
        if(req->request.post_data)
            aml_free(req->request.post_data);
        if(req->request.dependencies)
            aml_free(req->request.dependencies);
    }
    if (req->request.userdata && req->request.userdata_cleanup) {
        req->request.userdata_cleanup(req->request.userdata);
        req->request.userdata = NULL;
    }
    aml_free(req);
}

static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t total_size = size * nitems;
    curl_event_loop_request_t *req = (curl_event_loop_request_t *)userdata;

    // Check if this is the Content-Length header
    if (strncasecmp(buffer, "Content-Length:", 15) == 0) {
        long cl = atol(buffer + 15);
        if (cl > req->request.max_download_size) {
            fprintf(stderr, "[curl_event_loop] Content-Length exceeds max_download_size (%ld > %ld)\n", cl, req->request.max_download_size);
            // Signal libcurl to abort by returning 0
            return 0;
        }
    }
    return total_size;
}

long curl_event_request_content_length(curl_event_request_t *r) {
    curl_event_loop_request_t *req =
        (curl_event_loop_request_t *)((char *)r - offsetof(curl_event_loop_request_t, request));
    if(!req->content_length_found) {
        curl_off_t content_length = 0;
        curl_easy_getinfo(req->easy_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
        if (content_length >= 0) {
            req->content_length = (size_t)content_length;
        }
        req->content_length_found = true;
    }
    return req->content_length;
}


/**
 * Helper to set up the CURL easy handle with relevant options.
 */
static bool setup_curl_handle(curl_event_loop_request_t *req, curl_event_loop_t *loop) {
    req->easy_handle = NULL;
    req->content_length_found = false;
    req->content_length = -1;
    if(req->request.on_prepare) {
        if(!req->request.on_prepare(&req->request))
            return false;
    }
    req->easy_handle = curl_easy_init();
    if (!req->easy_handle) {
        fprintf(stderr, "[setup_curl_handle] curl_easy_init failed.\n");
        return false;
    }

    curl_easy_setopt(req->easy_handle, CURLOPT_URL, req->request.url);
    curl_easy_setopt(req->easy_handle, CURLOPT_ACCEPT_ENCODING, "");


    if(req->request.max_download_size > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(req->easy_handle, CURLOPT_HEADERDATA, req);
    }

    if (strcasecmp(req->request.method, "POST") == 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_POST, 1L);
        if (req->request.post_data) {
            curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDS, req->request.post_data);
        }
    } else if (strcasecmp(req->request.method, "PUT") == 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_UPLOAD, 1L);
        // For PUT with body, you'd provide a read callback, etc.
    }
    // etc. for other methods (DELETE, PATCH, etc.)

    if (req->request.headers) {
        curl_easy_setopt(req->easy_handle, CURLOPT_HTTPHEADER, req->request.headers);
    }

    // Timeouts
    if (req->request.connect_timeout > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_CONNECTTIMEOUT, req->request.connect_timeout);
    }
    if (req->request.transfer_timeout > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_TIMEOUT, req->request.transfer_timeout);
    }
    if (req->request.low_speed_limit > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_LOW_SPEED_LIMIT, req->request.low_speed_limit);
    }
    if (req->request.low_speed_time > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_LOW_SPEED_TIME, req->request.low_speed_time);
    }

    // HTTP/3
    if (loop->enable_http3) {
        curl_easy_setopt(req->easy_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
    }

    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEFUNCTION, (curl_write_callback)req->request.write_cb);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEDATA, &req->request);

    // Tie the request to this handle for later retrieval
    curl_easy_setopt(req->easy_handle, CURLOPT_PRIVATE, req);

    // Attach to shared handle for DNS caching
    curl_easy_setopt(req->easy_handle, CURLOPT_SHARE, loop->shared_handle);
    return true;
}


bool curl_event_loop_request_start(curl_event_loop_request_t *req) {
    curl_event_loop_t *loop = req->request.loop;
    if(req->request.rate_limit) {
        uint64_t next = rate_manager_start_request(req->request.rate_limit, req->request.rate_limit_high_priority);
        if(next) {
            req->request.next_retry_at = next;
            curl_event_request_insert(&loop->rate_limited_requests, req);
            return false;
        }
    }

    if(!setup_curl_handle(req, loop)) {
        // maybe retry?
        curl_event_request_destroy(req);
        return false;
    }
    curl_multi_add_handle(loop->multi_handle, req->easy_handle);
    req->request.request_start_time = macro_now();
    loop->num_multi_requests++;
    req->multi_handle = loop->multi_handle;
    curl_event_request_insert(&loop->queued_requests, req);
    loop->num_queued_requests++;
    return true;
}

curl_event_request_t *curl_event_loop_enqueue(curl_event_loop_t *loop, const curl_event_request_t *src_req, int priority) {
    if (!loop || !src_req || !src_req->url) {
        fprintf(stderr, "[curl_event_loop_enqueue] Invalid arguments.\n");
        return NULL;
    }

    // Create a new request object
    curl_event_loop_request_t *req = (curl_event_loop_request_t *)aml_calloc(1, sizeof(curl_event_loop_request_t));
    if (!req) {
        fprintf(stderr, "[curl_event_loop_enqueue] Memory allocation failed.\n");
        return NULL;
    }

    // Copy fields from src_req
    req->request.loop         = loop;
    req->is_injected          = false;
    req->request.url          = aml_strdup(src_req->url);
    req->request.method       = src_req->method ? aml_strdup(src_req->method) : (src_req->post_data ? aml_strdup("PUT") : aml_strdup("GET"));
    req->request.post_data    = src_req->post_data ? aml_strdup(src_req->post_data) : NULL;
    struct curl_slist *copy = NULL;
    for (struct curl_slist *h = src_req->headers; h; h = h->next) {
        copy = curl_slist_append(copy, h->data);
    }
    req->request.headers = copy;
    req->request.rate_limit  = src_req->rate_limit ? aml_strdup(src_req->rate_limit) : NULL;
    req->request.rate_limit_high_priority  = src_req->rate_limit_high_priority;
    req->request.on_prepare   = src_req->on_prepare;
    req->request.should_refresh   = src_req->should_refresh;
    req->request.on_failure   = src_req->on_failure;
    req->request.on_complete  = src_req->on_complete;
    req->request.write_cb     = src_req->write_cb;
    req->request.on_retry     = src_req->on_retry ? src_req->on_retry : default_calculate_retry;
    req->request.max_download_size = src_req->max_download_size;
    req->request.userdata     = src_req->userdata;
    req->request.userdata_cleanup = src_req->userdata_cleanup;
    req->request.dependencies = NULL;

    if(src_req->dependencies) {
        size_t length = 0, count = 0;
        char **p = src_req->dependencies;
        while(*p) {
            count++;
            length += strlen(*p) + 1;
            p++;
        }
        if(count) {
            req->request.dependencies = (char **)aml_calloc(1,sizeof(char *) * (count + 1) + length);
            char *q = (char *)(req->request.dependencies + count + 1);
            p = src_req->dependencies;
            count = 0;
            while(*p) {
                req->request.dependencies[count] = q;
                count++;
                strcpy(q, *p);
                q += strlen(*p) + 1;
                p++;
            }
            req->request.dependencies[count] = NULL;
        }
    }

    req->request.connect_timeout   = src_req->connect_timeout;
    req->request.transfer_timeout  = src_req->transfer_timeout;
    req->request.low_speed_limit   = src_req->low_speed_limit;
    req->request.low_speed_time    = src_req->low_speed_time;
    req->request.max_retries       = src_req->max_retries;
    req->request.current_retries   = 0;
    req->request.backoff_factor    = (src_req->backoff_factor > 0.0) ? src_req->backoff_factor : 2.0;


    req->request.next_retry_at = macro_now();
    req->request.start_time = req->request.next_retry_at;
    req->request.request_start_time = req->request.next_retry_at;
    req->request.next_retry_at -= ((int64_t)priority * 1000000000);

    pthread_mutex_lock(&loop->mutex);
    req->next_pending = loop->pending_requests;
    loop->pending_requests = req;
    loop->metrics.total_requests++;
    pthread_mutex_unlock(&loop->mutex);
    return &req->request;
}

/**
 * Calculate next retry time with a simple exponential backoff + jitter approach.
 */
static uint64_t calculate_next_retry(int current_retries, double backoff_factor) {
    uint64_t now = macro_now();

    // Calculate base backoff = backoff_factor^current_retries
    double base = pow(backoff_factor, current_retries);

    // Add random jitter [0, base]
    double jitter = ((double)rand() / (double)RAND_MAX) * base;

    // Convert total delay to nanoseconds
    uint64_t total_delay_ns = (uint64_t)((base + jitter) * 1e9);

    return now + total_delay_ns; // Return next retry time in nanoseconds
}

static bool default_calculate_retry(curl_event_request_t *req) {
    if (req->max_retries == -1 || req->max_retries > req->current_retries) {
        req->current_retries++;
        req->next_retry_at = calculate_next_retry(req->current_retries, req->backoff_factor);
        return true;
    }
    return false;
}

double curl_event_request_time_spent(const curl_event_request_t *r) {
    return macro_time_diff(macro_now(), r->start_time);
}

double curl_event_request_time_spent_on_request(const curl_event_request_t *r) {
    return macro_time_diff(macro_now(), r->request_start_time);
}

void curl_event_loop_update_header(curl_event_request_t *req, const char *header_name, const char *new_value) {
    struct curl_slist *new_list = NULL;
    struct curl_slist *current = req->headers;
    size_t name_len = strlen(header_name);

    aml_pool_t *pool = req->loop->pool;
    aml_pool_marker_t mark;
    aml_pool_save(pool, &mark);

    bool found = false;
    while (current) {
        // Check if the header starts with the header name (case-sensitive)
        if (strncmp(current->data, header_name, name_len) == 0 && current->data[name_len] == ':') {
            // Replace this header
            new_list = curl_slist_append(new_list, aml_pool_strdupf(pool, "%s: %s", header_name, new_value));
            found = true;
        } else {
            // Copy the existing header as-is
            new_list = curl_slist_append(new_list, current->data);
        }
        current = current->next;
    }
    if(!found) {
        new_list = curl_slist_append(new_list, aml_pool_strdupf(pool, "%s: %s", header_name, new_value));
    }
    curl_slist_free_all(req->headers);
    req->headers = new_list;
    aml_pool_restore(pool, &mark);
}