// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/google_vision.h"
#include "a-curl-library/rate_manager.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include "a-json-library/ajson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Base URL for Google Vision API (without API key) */
static const char *GOOGLE_VISION_BASE_URL = "https://vision.googleapis.com/v1/images:annotate";

void curl_event_plugin_google_vision_set_rate() {
    /* Set an appropriate rate limit for Google Vision requests */
    rate_manager_set_limit("google_vision", 5, 10.0);
}

/**
 * Prepare the request by retrieving the API key from dependency state
 * and appending it to the base URL as a query parameter.
 */
static bool google_vision_on_prepare(curl_event_request_t *req) {
    /* Retrieve the API key from state (using the first dependency key) */
    char *api_key = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!api_key) {
        fprintf(stderr, "[google_vision] Missing API key.\n");
        return false;
    }

    /* Append the API key to the base URL. New URL format:
       "https://vision.googleapis.com/v1/images:annotate?key=<api_key>" */
    char *old_url = req->url;
    char *new_url = aml_strdupf("%s?key=%s", old_url, api_key);
    aml_free(old_url);
    req->url = new_url;
    aml_free(api_key);
    return true;
}

curl_event_request_t *curl_event_plugin_google_vision_init(
    curl_event_loop_t *loop,
    const char *api_key_state_key,
    const char *image_url,
    curl_output_interface_t *output_interface
) {
    if (!loop || !api_key_state_key || !image_url || !output_interface) {
        fprintf(stderr, "[google_vision_init] Invalid arguments.\n");
        return NULL;
    }

    /* Create a memory pool for constructing the URL and JSON body */
    aml_pool_t *pool = aml_pool_init(16 * 1024);

    /* Duplicate the base URL (API key will be appended later) */
    char *url = aml_pool_strdupf(pool, "%s", GOOGLE_VISION_BASE_URL);

    /* Prepare the dependency array (null-terminated) with the API key state key */
    const char *dependencies[2] = { api_key_state_key, NULL };

    /* Build the JSON request payload for web detection.
       The structure is:
       {
           "requests": [
               {
                   "image": {
                       "source": {
                           "imageUri": "<image_url>"
                       }
                   },
                   "features": [
                       {
                           "type": "WEB_DETECTION"
                       }
                   ]
               }
           ]
       }
    */
    ajson_t *root = ajsono(pool);
    ajson_t *requests_array = ajsona(pool);
    ajson_t *request_obj = ajsono(pool);

    ajson_t *image_obj = ajsono(pool);
    ajson_t *source_obj = ajsono(pool);
    ajsono_append(source_obj, "imageUri", ajson_encode_str(pool, image_url), false);
    ajsono_append(image_obj, "source", source_obj, false);
    ajsono_append(request_obj, "image", image_obj, false);

    ajson_t *features_array = ajsona(pool);
    ajson_t *feature_obj = ajsono(pool);
    ajsono_append(feature_obj, "type", ajson_encode_str(pool, "WEB_DETECTION"), false);
    ajsona_append(features_array, feature_obj);
    ajsono_append(request_obj, "features", features_array, false);

    ajsona_append(requests_array, request_obj);
    ajsono_append(root, "requests", requests_array, false);

    /* Stringify the JSON payload */
    char *post_data = ajson_stringify(pool, root);

    /* Set up the request */
    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.rate_limit = "google_vision";
    req.method = "POST";
    req.dependencies = (char **)dependencies;
    req.post_data = post_data;
    req.on_prepare = google_vision_on_prepare;
    curl_output_defaults(&req, output_interface);

    req.low_speed_limit = 1024; // 1 KB/s
    req.low_speed_time = 15;
    req.max_retries = 3;        // Retry up to 3 times

    /* Enqueue the request */
    curl_event_request_t *r = curl_event_loop_enqueue(loop, &req, 0);
    if (!r) {
        fprintf(stderr, "[google_vision_init] Failed to enqueue request.\n");
    }
    aml_pool_destroy(pool);
    return r;
}