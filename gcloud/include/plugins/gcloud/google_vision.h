// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _CURL_EVENT_PLUGIN_GOOGLE_VISION_H
#define _CURL_EVENT_PLUGIN_GOOGLE_VISION_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_output.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sets the rate limit for Google Vision API requests. */
void curl_event_plugin_google_vision_set_rate(void);

/**
 * Initialize a Google Vision API web-detection request.
 *
 * @param loop             Event loop.
 * @param api_key_id       Resource id whose payload is a NUL-terminated API key string.
 * @param image_url        Image URL to analyze.
 * @param output_interface Output interface to receive the response body.
 * @return enqueued request or NULL on failure.
 */
curl_event_request_t *curl_event_plugin_google_vision_init(
    curl_event_loop_t *loop,
    curl_event_res_id  api_key_id,
    const char *image_url,
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // _CURL_EVENT_PLUGIN_GOOGLE_VISION_H
