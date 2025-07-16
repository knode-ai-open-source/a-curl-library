// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/google_custom_search.h"
#include "a-curl-library/rate_manager.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Base URL without API key; the key will be appended in the prepare function */
static const char *GOOGLE_CUSTOM_SEARCH_URL_FORMAT = "https://www.googleapis.com/customsearch/v1?cx=%s&q=%s";

void curl_event_plugin_google_custom_search_set_rate() {
    /* Set an appropriate rate limit for Google Custom Search requests */
    rate_manager_set_limit("google_custom_search", 5, 9.0);
}

/**
 * Prepare the request by retrieving the API key from the dependency state,
 * then rebuilding the URL to include the key.
 */
static bool google_custom_search_on_prepare(curl_event_request_t *req) {
    /* Retrieve the API key from state (using the first dependency key) */
    char *api_key = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!api_key) {
        fprintf(stderr, "[google_custom_search] Missing API key.\n");
        return false;
    }

    /* Rebuild the URL to include the API key as a query parameter.
       The new URL format will be: "<old_url>&key=<api_key>" */
    char *old_url = req->url;
    char *new_url = aml_strdupf("%s&key=%s", old_url, api_key);
    aml_free(old_url);
    req->url = new_url;
    aml_free(api_key);
    return true;
}

curl_event_request_t *curl_event_plugin_google_custom_search_init(
    curl_event_loop_t *loop,
    const char *api_key_state_key,
    const char *search_engine_id,
    const char *query,
    curl_output_interface_t *output_interface
) {
    if (!loop || !api_key_state_key || !search_engine_id || !query || !output_interface) {
        fprintf(stderr, "[google_custom_search_init] Invalid arguments.\n");
        return NULL;
    }

    /* Initialize a memory pool for constructing the URL */
    aml_pool_t *pool = aml_pool_init(16 * 1024);
    /* Build the initial URL without the API key */
    char *url = aml_pool_strdupf(pool, GOOGLE_CUSTOM_SEARCH_URL_FORMAT, search_engine_id, query);

    /* Prepare the dependency array (null-terminated) with the API key state key */
    const char *dependencies[2] = { api_key_state_key, NULL };

    /* Set up the request structure */
    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.rate_limit = "google_custom_search";
    /* Google Custom Search uses GET requests */
    req.method = "GET";
    req.dependencies = (char **)dependencies;
    req.on_prepare = google_custom_search_on_prepare;
    curl_output_defaults(&req, output_interface);

    req.low_speed_limit = 1024; // 1 KB/s
    req.low_speed_time = 15;
    req.max_retries = 3; // Retry up to 3 times

    /* Enqueue the request */
    curl_event_request_t *r = curl_event_loop_enqueue(loop, &req, 0);
    if (!r) {
        fprintf(stderr, "[google_custom_search_init] Failed to enqueue request.\n");
    }
    aml_pool_destroy(pool);
    return r;
}