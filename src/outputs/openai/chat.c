// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/outputs/openai/chat.h"
#include "a-curl-library/plugins/openai/chat.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    curl_output_interface_t interface;
    aml_pool_t *pool;
    aml_buffer_t *response_buffer;
    openai_chat_complete_callback_t complete_callback;
    void *complete_callback_arg;
} openai_chat_output_t;

// Write callback: Store response data in a buffer
static size_t chat_write_callback(const void *data, size_t size, size_t nmemb, curl_output_interface_t *interface) {
    openai_chat_output_t *output = (openai_chat_output_t *)interface;
    size_t total = size * nmemb;
    aml_buffer_append(output->response_buffer, data, total);
    return total;
}

// Extract an integer value from JSON safely
static int extract_json_int(ajson_t *node, const char *key, int default_value) {
    return ajson_to_int(ajsono_scan(node, key), default_value);
}

// **Parse OpenAI Chat Completion Response**
static void parse_openai_chat_response(curl_output_interface_t *output_data, curl_event_request_t *req) {
    openai_chat_output_t *output = (openai_chat_output_t *)output_data;

    // Parse JSON response
    ajson_t *json = ajson_parse_string(output->pool, aml_buffer_data(output->response_buffer));
    if (!json || ajson_is_error(json)) {
        fprintf(stderr, "[parse_openai_chat_response] Failed to parse JSON response.\n");
        output->complete_callback(output->complete_callback_arg, req, false, NULL, -1, -1, -1);
        return;
    }

    // Extract token usage
    int prompt_tokens = extract_json_int(ajsono_scan(json, "usage"), "prompt_tokens", -1);
    int completion_tokens = extract_json_int(ajsono_scan(json, "usage"), "completion_tokens", -1);
    int total_tokens = extract_json_int(ajsono_scan(json, "usage"), "total_tokens", -1);

    // Extract assistant's response
    ajson_t *choices = ajsono_scan(json, "choices");
    char *assistant_message = NULL;
    if (choices && ajson_is_array(choices) && ajsona_count(choices) > 0) {
        ajson_t *message_node = ajsono_scan(ajsona_first(choices)->value, "message");
        assistant_message = ajsono_scan_strd(output->pool, message_node, "content", NULL);
    }

    // Call the completion callback with parsed data
    output->complete_callback(output->complete_callback_arg, req, true, assistant_message, prompt_tokens, completion_tokens, total_tokens);
}

// **Failure Callback**
static void chat_on_failure(CURLcode result, long http_code, curl_output_interface_t *output_data, curl_event_request_t *req) {
    openai_chat_output_t *output = (openai_chat_output_t *)output_data;
    fprintf(stderr, "[chat_on_failure] HTTP %ld, CURL code %d\n", http_code, result);
    output->complete_callback(output->complete_callback_arg, req, false, NULL, -1, -1, -1);
}

// **Initialization Function**
static bool chat_init(curl_output_interface_t *interface, long content_length) {
    (void)content_length;
    openai_chat_output_t *output = (openai_chat_output_t *)interface;
    output->pool = aml_pool_init(8192);
    output->response_buffer = aml_buffer_init(2048);
    return true;
}

// **Destroy Function**
static void chat_destroy(curl_output_interface_t *interface) {
    openai_chat_output_t *output = (openai_chat_output_t *)interface;
    if (output->pool) {
        aml_pool_destroy(output->pool);
    }
    if (output->response_buffer) {
        aml_buffer_destroy(output->response_buffer);
    }
    aml_free(output);
}

// **OpenAI Chat Output**
curl_output_interface_t *openai_chat_output(
    openai_chat_complete_callback_t complete_callback,
    void *complete_callback_arg) {

    openai_chat_output_t *output = aml_calloc(1, sizeof(openai_chat_output_t));
    if (!output) return NULL;

    output->complete_callback = complete_callback;
    output->complete_callback_arg = complete_callback_arg;
    output->interface.init = chat_init;
    output->interface.write = chat_write_callback;
    output->interface.failure = chat_on_failure;
    output->interface.complete = parse_openai_chat_response;
    output->interface.destroy = chat_destroy;

    return (curl_output_interface_t *)output;
}
