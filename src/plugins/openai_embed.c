// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/openai_embed.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *OPENAI_EMBEDDING_URL = "https://api.openai.com/v1/embeddings";

/**
 * Prepare the request by setting the Authorization header.
 */
static bool openai_embed_on_prepare(curl_event_request_t *req) {
    // Get the API key from the event loop state
    char *api_key = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!api_key) {
        fprintf(stderr, "[openai_embed] Missing API key.\n");
        return false;
    }

    // Set the Authorization and Content-Type headers
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    aml_free(api_key);
    return true;
}

curl_event_request_t *curl_event_plugin_openai_embed_init(
    curl_event_loop_t *loop,
    const char *token_state_key,
    const char *model_id,
    int dimensions,
    const char **input_texts,
    size_t num_texts,
    curl_output_interface_t *output_interface
) {
    if (!loop || !token_state_key || !model_id || !input_texts || num_texts == 0 || !output_interface) {
        fprintf(stderr, "[openai_embed_init] Invalid arguments.\n");
        return NULL;
    }

    aml_pool_t *pool = aml_pool_init(16 * 1024);

    // Prepare dependencies array (null-terminated)
    const char *dependencies[2] = { token_state_key, NULL };

    // Build JSON request payload using ajson
    ajson_t *root = ajsono(pool);
    ajsono_append(root, "model", ajson_encode_str(pool, model_id), false);
    ajsono_append(root, "encoding_format", ajson_encode_str(pool, "float"), false);
    ajsono_append(root, "dimensions", ajson_number(pool, dimensions), false);

    // Build input array
    ajson_t *arr = ajsona(pool);
    for (size_t i = 0; i < num_texts; i++) {
        ajsona_append(arr, ajson_encode_str(pool, input_texts[i]));
    }
    ajsono_append(root, "input", arr, false);

    // Prepare the request
    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = (char *)OPENAI_EMBEDDING_URL;
    req.method = "POST";
    req.dependencies = (char **)dependencies;

    req.post_data = ajson_stringify(pool, root);
    fprintf(stderr, "%s\n", req.post_data);
    req.on_prepare = openai_embed_on_prepare;
    curl_output_defaults(&req, output_interface);

    req.low_speed_limit = 1024; // 1 KB/s
    req.low_speed_time = 60;    // 60 seconds
    req.max_retries = 3;        // Retry up to 3 times

    // Enqueue the request
    curl_event_request_t *r = curl_event_loop_enqueue(loop, &req, 0);
    if(!r)
        fprintf(stderr, "[openai_embed_init] Failed to enqueue request.\n");
    aml_pool_destroy(pool);
    return r;
}