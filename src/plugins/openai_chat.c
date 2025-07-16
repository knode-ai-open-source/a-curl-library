// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/openai_chat.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *OPENAI_CHAT_URL = "https://api.openai.com/v1/chat/completions";

/**
 * Prepare the request by setting the Authorization header.
 */
static bool openai_chat_on_prepare(curl_event_request_t *req) {
    // Get the API key from the event loop state
    char *api_key = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!api_key) {
        fprintf(stderr, "[openai_chat] Missing API key.\n");
        return false;
    }

    // Set the Authorization and Content-Type headers
    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    aml_free(api_key);
    return true;
}

bool curl_event_plugin_openai_chat_init(
    curl_event_loop_t *loop,
    const char *token_state_key,
    const char *model_id,
    float temperature,
    int max_tokens,
    char **messages,  // Must be an array of strings, formatted as "role: content"
    size_t num_messages,
    int delay_ms,
    curl_output_interface_t *output_interface
) {
    if (!loop || !token_state_key || !model_id || !messages || num_messages == 0 || !output_interface) {
        fprintf(stderr, "[openai_chat_init] Invalid arguments.\n");
        return false;
    }

    aml_pool_t *pool = aml_pool_init(16 * 1024);

    // Prepare dependencies array (null-terminated)
    const char *dependencies[2] = { token_state_key, NULL };

    // Build JSON request payload using ajson
    ajson_t *root = ajsono(pool);
    ajsono_append(root, "model", ajson_encode_str(pool, model_id), false);
    if(!strncmp(model_id, "gpt", 3))
        ajsono_append(root, "temperature", ajson_number_stringf(pool, "%0.2f", temperature), false);
    if(max_tokens) {
        if(!strncmp(model_id, "gpt", 3))
            ajsono_append(root, "max_tokens", ajson_number(pool, max_tokens), false);
        else
            ajsono_append(root, "max_completion_tokens", ajson_number(pool, max_tokens), false);
    }
    // Build message array
    ajson_t *arr = ajsona(pool);
    for (size_t i = 0; i < num_messages; i++) {
        char *role = (char *)messages[i];
        const char *content = strchr(role, ' ');
        if(!content || content == role)
            continue;
        size_t role_length = content-role;
        content++;
        role = (char *)aml_pool_dup(pool, role, role_length+1);
        role[role_length] = 0;
        if (role && content) {
            ajson_t *msg_obj = ajsono(pool);
            ajsono_append(msg_obj, "role", ajson_encode_str(pool, role), false);
            ajsono_append(msg_obj, "content", ajson_encode_str(pool, content), false);
            ajsona_append(arr, msg_obj);
        }
    }
    ajsono_append(root, "messages", arr, false);

    // Prepare the request
    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = (char *)OPENAI_CHAT_URL;
    req.method = (char *)"POST";
    req.dependencies = (char **)dependencies;

    req.post_data = ajson_stringify(pool, root);

    req.on_prepare = openai_chat_on_prepare;
    curl_output_defaults(&req, output_interface);

    req.low_speed_limit = 1024; // 1 KB/s
    req.low_speed_time = 60;    // 60 seconds
    req.max_retries = 3;        // Retry up to 3 times

    // Enqueue the request
    if (!curl_event_loop_enqueue(loop, &req, -delay_ms)) {
        fprintf(stderr, "[openai_chat_init] Failed to enqueue request.\n");
        aml_pool_destroy(pool);
        return false;
    }

    aml_pool_destroy(pool);
    return true;
}