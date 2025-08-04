// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _CURL_EVENT_PLUGIN_GOOGLE_CUSTOM_SEARCH_H
#define _CURL_EVENT_PLUGIN_GOOGLE_CUSTOM_SEARCH_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_output.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sets the rate limit for Google Custom Search requests.
 * (name: "google_custom_search")
 */
void curl_event_plugin_google_custom_search_set_rate();

/**
 * Initializes a Google Custom Search API request.
 *
 * @param loop             The event loop.
 * @param api_key_id       Resource id that holds a NUL-terminated API key string.
 *                         (Publish with curl_event_res_publish_str or register_async.)
 * @param search_engine_id The custom search engine ID (cx).
 * @param query            The search query (must already be URL-encoded).
 * @param output_interface The output interface for handling the response.
 * @return Pointer to the enqueued request, or NULL on failure.
 */
curl_event_request_t *curl_event_plugin_google_custom_search_init(
    curl_event_loop_t *loop,
    curl_event_res_id  api_key_id,
    const char *search_engine_id,
    const char *query,
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // _CURL_EVENT_PLUGIN_GOOGLE_CUSTOM_SEARCH_H
