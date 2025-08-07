// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/sinks/openai/v1/chat/completions.h"
#include "a-curl-library/plugins/openai/v1/chat/completions.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    curl_sink_interface_t interface;
    aml_buffer_t *response_buffer;
    openai_v1_chat_completions_complete_callback_t complete_callback;
    void *complete_callback_arg;
} openai_v1_chat_completions_sink_t;

// Write callback: Store response data in a buffer
static size_t chat_write_callback(const void *data, size_t size, size_t nmemb, curl_sink_interface_t *interface) {
    openai_v1_chat_completions_sink_t *sink = (openai_v1_chat_completions_sink_t *)interface;
    size_t total = size * nmemb;
    aml_buffer_append(sink->response_buffer, data, total);
    return total;
}

// Extract an integer value from JSON safely
static int extract_json_int(ajson_t *node, const char *key, int default_value) {
    return ajson_to_int(ajsono_scan(node, key), default_value);
}

// **Parse OpenAI Chat Completion Response**
static void parse_openai_v1_chat_completions_response(curl_sink_interface_t *sink_data, curl_event_request_t *req) {
    openai_v1_chat_completions_sink_t *sink = (openai_v1_chat_completions_sink_t *)sink_data;

    aml_pool_t *pool = sink_data->pool;

    // Parse JSON response
    ajson_t *json = ajson_parse_string(pool, aml_buffer_data(sink->response_buffer));
    if (!json || ajson_is_error(json)) {
        fprintf(stderr, "[parse_openai_v1_chat_completions_response] Failed to parse JSON response.\n");
        sink->complete_callback(sink->complete_callback_arg, req, false, NULL, -1, -1, -1);
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
        assistant_message = ajsono_scan_strd(pool, message_node, "content", NULL);
    }

    // Call the completion callback with parsed data
    sink->complete_callback(sink->complete_callback_arg, req, true, assistant_message, prompt_tokens, completion_tokens, total_tokens);
}

// **Failure Callback**
static void chat_on_failure(CURLcode result, long http_code, curl_sink_interface_t *sink_data, curl_event_request_t *req) {
    openai_v1_chat_completions_sink_t *sink = (openai_v1_chat_completions_sink_t *)sink_data;
    fprintf(stderr, "[chat_on_failure] HTTP %ld, CURL code %d\n", http_code, result);
    sink->complete_callback(sink->complete_callback_arg, req, false, NULL, -1, -1, -1);
}

// **Initialization Function**
static bool chat_init(curl_sink_interface_t *interface, long content_length) {
    (void)content_length;
    openai_v1_chat_completions_sink_t *sink = (openai_v1_chat_completions_sink_t *)interface;
    sink->response_buffer = aml_buffer_init(2048);
    return true;
}

// **Destroy Function**
static void chat_destroy(curl_sink_interface_t *interface) {
    openai_v1_chat_completions_sink_t *sink = (openai_v1_chat_completions_sink_t *)interface;
    if (sink->response_buffer) {
        aml_buffer_destroy(sink->response_buffer);
    }
    aml_free(sink);
}

// **OpenAI Chat sink**
curl_sink_interface_t *openai_v1_chat_completions_sink(
    curl_event_request_t *req,
    openai_v1_chat_completions_complete_callback_t complete_callback,
    void *complete_callback_arg) {

    openai_v1_chat_completions_sink_t *sink =
        aml_pool_zalloc(req->pool, sizeof(openai_v1_chat_completions_sink_t));
    if (!sink) return NULL;

    sink->complete_callback = complete_callback;
    sink->complete_callback_arg = complete_callback_arg;
    sink->interface.pool = req->pool;
    sink->interface.init = chat_init;
    sink->interface.write = chat_write_callback;
    sink->interface.failure = chat_on_failure;
    sink->interface.complete = parse_openai_v1_chat_completions_response;
    sink->interface.destroy = chat_destroy;

    curl_event_request_sink(req, (curl_sink_interface_t *)sink, NULL);

    return (curl_sink_interface_t *)sink;
}
