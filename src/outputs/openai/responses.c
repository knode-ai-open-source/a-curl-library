// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/outputs/openai/responses.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_buffer.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    curl_output_interface_t interface;
    curl_event_res_id api_key_id;
    curl_event_res_id prev_id_res;
    ajson_t          *msg_array;

    aml_pool_t           *pool;
    aml_buffer_t         *response_buffer;
    openai_responses_complete_callback_t cb;
    void                *cb_arg;
} openai_responses_output_t;

static size_t _write_cb(const void *data, size_t size, size_t nmemb, curl_output_interface_t *iface) {
    openai_responses_output_t *o = (void*)iface;
    aml_buffer_append(o->response_buffer, data, size*nmemb);
    return size*nmemb;
}

static int _get_int(ajson_t *n, const char *k, int d) {
    return ajson_to_int(ajsono_scan(n, k), d);
}

static void _on_complete(curl_output_interface_t *iface, curl_event_request_t *req) {
    openai_responses_output_t *o = (void*)iface;
    const char *raw = aml_buffer_data(o->response_buffer);

    // DEBUG: print the unparsed JSON
    fprintf(stderr, "[DEBUG raw response] %s\n", raw);

    // Parse JSON
    ajson_t *json = ajson_parse_string(o->pool, raw);
    if (!json || ajson_is_error(json)) {
        fprintf(stderr, "[responses] JSON parse error\n");
        o->cb(o->cb_arg, req, false, NULL, -1, -1, -1);
        return;
    }

    // Extract text from output[0].content[0].text
    char *out = NULL;
    ajson_t *out_arr = ajsono_scan(json, "output");
    if (out_arr && ajson_is_array(out_arr) && ajsona_count(out_arr) > 0) {
        ajson_t *first_msg = ajsona_first(out_arr)->value;
        ajson_t *cont_arr  = ajsono_scan(first_msg, "content");
        if (cont_arr && ajson_is_array(cont_arr) && ajsona_count(cont_arr) > 0) {
            ajson_t *first_cont = ajsona_first(cont_arr)->value;
            out = ajsono_scan_strd(o->pool, first_cont, "text", NULL);
        }
    }

    // Read usage fields: input_tokens, output_tokens, total_tokens
    ajson_t *usage = ajsono_scan(json, "usage");
    int prompt_tokens     = _get_int(usage, "input_tokens",  -1);
    int completion_tokens = _get_int(usage, "output_tokens", -1);
    int total_tokens      = _get_int(usage, "total_tokens",  -1);

    o->cb(o->cb_arg, req, true, out, prompt_tokens, completion_tokens, total_tokens);
}

static void _on_failure(CURLcode r, long http, curl_output_interface_t *iface, curl_event_request_t *req) {
    openai_responses_output_t *o = (void*)iface;
    fprintf(stderr, "[responses] HTTP %ld, CURL %d\n", http, r);
    // print the body we got back (usually a JSON error with a message)
    if (o->response_buffer) {
        const char *body = aml_buffer_data(o->response_buffer);
        if (body && *body)
            fprintf(stderr, "  → body: %s\n", body);
    }

    o->cb(o->cb_arg, req, false, NULL, -1, -1, -1);
}

static bool _init(curl_output_interface_t *iface, long _len) {
    openai_responses_output_t *o = (void*)iface;
    o->pool = aml_pool_init(8*1024);
    o->response_buffer = aml_buffer_init(2*1024);
    return true;
}

static void _destroy(curl_output_interface_t *iface) {
    openai_responses_output_t *o = (void*)iface;
    aml_pool_destroy(o->pool);
    aml_buffer_destroy(o->response_buffer);
    aml_free(o);
}

curl_output_interface_t *openai_responses_output(
    openai_responses_complete_callback_t cb,
    void *cb_arg
) {
    openai_responses_output_t *o = aml_calloc(1, sizeof(*o));
    o->cb     = cb;
    o->cb_arg = cb_arg;
    o->interface.init     = _init;
    o->interface.write    = _write_cb;
    o->interface.complete = _on_complete;
    o->interface.failure  = _on_failure;
    o->interface.destroy  = _destroy;
    return (void*)o;
}
