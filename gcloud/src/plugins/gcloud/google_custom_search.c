// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcloud/google_custom_search.h"
#include "a-curl-library/rate_manager.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_event_request.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Base URL without API key; the key is appended in on_prepare */
static const char *GOOGLE_CUSTOM_SEARCH_URL_FORMAT =
    "https://www.googleapis.com/customsearch/v1?cx=%s&q=%s";

void curl_event_plugin_google_custom_search_set_rate(void) {
    /* name, bucket, refill per second (token bucket chosen elsewhere) */
    rate_manager_set_limit("google_custom_search", 5, 9.0);
}

/* Per-request context so we own a mutable URL */
typedef struct gcs_ctx_s {
    curl_event_res_id api_key_id;
    char *url;  /* owned; rebuilt in on_prepare to append &key=... */
} gcs_ctx_t;

static void gcs_ctx_destroy(void *userdata) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->url) aml_free(ctx->url);
    aml_free(ctx);
}

/**
 * Prepare the request by retrieving the API key from the dependency resource,
 * then rebuilding the URL to include the key.
 */
static bool google_custom_search_on_prepare(curl_event_request_t *req) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)req->userdata;

    /* Peek the NUL-terminated API key string (no copy, no free). */
    const char *api_key = (const char *)curl_event_res_peek(req->loop, ctx->api_key_id);
    if (!api_key || !*api_key) {
        fprintf(stderr, "[google_custom_search] Missing API key (resource not ready/failed).\n");
        return false;
    }

    /* Rebuild the URL to include the API key as a query parameter. */
    char *new_url = aml_strdupf("%s&key=%s", ctx->url, api_key);
    if (!new_url) {
        fprintf(stderr, "[google_custom_search] OOM building URL.\n");
        return false;
    }
    aml_free(ctx->url);
    ctx->url = new_url;
    req->url = ctx->url;
    return true;
}

curl_event_request_t *curl_event_plugin_google_custom_search_init(
    curl_event_loop_t *loop,
    curl_event_res_id  api_key_id,
    const char *search_engine_id,
    const char *query,
    curl_output_interface_t *output_interface
) {
    if (!loop || api_key_id == 0 || !search_engine_id || !query || !output_interface) {
        fprintf(stderr, "[google_custom_search_init] Invalid arguments.\n");
        return NULL;
    }

    /* Build the initial URL (without key) on the heap, since we'll mutate later. */
    char *url = aml_strdupf(GOOGLE_CUSTOM_SEARCH_URL_FORMAT, search_engine_id, query);
    if (!url) {
        fprintf(stderr, "[google_custom_search_init] OOM building base URL.\n");
        return NULL;
    }

    gcs_ctx_t *ctx = (gcs_ctx_t *)aml_calloc(1, sizeof(*ctx));
    if (!ctx) {
        aml_free(url);
        fprintf(stderr, "[google_custom_search_init] OOM.\n");
        return NULL;
    }
    ctx->api_key_id = api_key_id;
    ctx->url        = url;

    /* Set up the request structure */
    curl_event_request_t req = (curl_event_request_t){0};
    req.loop         = loop;
    req.url          = ctx->url;              /* on_prepare will replace it with &key=... */
    req.method       = "GET";
    req.on_prepare   = google_custom_search_on_prepare;
    req.userdata     = ctx;
    req.userdata_cleanup = gcs_ctx_destroy;

    /* Rate limiting */
    req.rate_limit   = "google_custom_search";

    /* Output interface defaults */
    curl_output_defaults(&req, output_interface);

    /* Timeouts / retries */
    req.low_speed_limit = 1024;  /* 1 KB/s */
    req.low_speed_time  = 15;
    req.max_retries     = 3;

    /* Block on the API key resource */
    curl_event_request_depend(&req, api_key_id);

    /* Enqueue the request */
    curl_event_request_t *r = curl_event_loop_enqueue(loop, &req, 0);
    if (!r) {
        gcs_ctx_destroy(ctx);
        fprintf(stderr, "[google_custom_search_init] Failed to enqueue request.\n");
        return NULL;
    }
    return r;
}
