// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/google_embed.h"
#include "a-curl-library/rate_manager.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GOOGLE_EMBEDDING_URL_FORMAT = "https://us-central1-aiplatform.googleapis.com/v1/projects/%s/locations/us-central1/publishers/google/models/%s:predict";

void curl_event_plugin_google_embed_set_rate() {
    rate_manager_set_limit("google_embed", 50, 24.5);
}

/**
 * Prepare the request by setting the Authorization header.
 */
static bool google_embed_on_prepare(curl_event_request_t *req) {
    // Get the access token from the event loop state
    char *access_token = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!access_token) {
        fprintf(stderr, "[google_embed] Missing access token.\n");
        return false;
    }

    // Set the Authorization header
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    aml_free(access_token);
    return true;
}

curl_event_request_t *curl_event_plugin_google_embed_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *model_id,
    int output_dimensionality,
    const char *token_state_key,
    char **input_text,
    size_t num_texts,
    curl_output_interface_t *output_interface
) {
    if (!loop || !project_id || !model_id || !token_state_key || !input_text || !output_interface) {
        fprintf(stderr, "[google_embed_init] Invalid arguments.\n");
        return NULL;
    }

    aml_pool_t *pool = aml_pool_init(16*1024);
    char *url = aml_pool_strdupf(pool, GOOGLE_EMBEDDING_URL_FORMAT, project_id, model_id);

    // Prepare dependencies array (null-terminated)
    const char *dependencies[2] = { token_state_key, NULL };

    ajson_t *root = ajsono(pool);
    ajson_t *parameters = ajsono(pool);
    ajsono_append(parameters, "outputDimensionality", ajson_number(pool, output_dimensionality), false);
    ajsono_append(root, "parameters", parameters, false);
    ajson_t *arr = ajsona(pool);
    for(size_t i=0; i<num_texts; i++) {
        ajson_t *obj = ajsono(pool);
        ajsono_append(obj, "content", ajson_encode_str(pool, input_text[i]), false);
        ajsona_append(arr, obj);
    }
    ajsono_append(root, "instances", arr, false);

    // Prepare the request
    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.rate_limit = "google_embed";
    req.method = "POST";
    req.dependencies = (char **)dependencies;

    req.post_data = ajson_stringify(pool, root);
    // printf( "%s\n", req.post_data);
    req.on_prepare = google_embed_on_prepare;
    curl_output_defaults(&req, output_interface);

    req.low_speed_limit = 1024; // 1 KB/s
    req.low_speed_time = 15;
    req.max_retries = 3;        // Retry up to 3 times

    // Enqueue the request
    curl_event_request_t *r = curl_event_loop_enqueue(loop, &req, 0);
    if(!r)
        fprintf(stderr, "[google_embed_init] Failed to enqueue request.\n");
    aml_pool_destroy(pool);
    return r;
}
