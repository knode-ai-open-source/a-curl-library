// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _CURL_EVENT_PLUGIN_GOOGLE_VISION_H
#define _CURL_EVENT_PLUGIN_GOOGLE_VISION_H

#include "a-curl-library/curl_event_loop.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sets the rate limit for Google Vision API requests.
 */
void curl_event_plugin_google_vision_set_rate();

/**
 * Initializes a Google Vision API request for web detection.
 *
 * @param loop              The event loop.
 * @param api_key_state_key The state key where the API key is stored.
 * @param image_url         The URL of the image to analyze.
 * @param output_interface  The output interface for handling the response.
 * @return A pointer to the enqueued request or NULL on failure.
 */
curl_event_request_t *curl_event_plugin_google_vision_init(
    curl_event_loop_t *loop,
    const char *api_key_state_key,
    const char *image_url,
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // _CURL_EVENT_PLUGIN_GOOGLE_VISION_H