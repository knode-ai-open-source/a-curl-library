// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// OpenAI “/v1/responses” – sink sink
// ─────────────────────────────────────────────────────────────────────────────
#include "a-curl-library/sinks/openai/v1/responses.h"

#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_buffer.h"

#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------- */
/*  Sink structure (lives in req->sink_data)                                */
/* -------------------------------------------------------------------------- */
typedef struct {
    curl_sink_interface_t interface;      /* - first field - */

    aml_buffer_t     *response_buffer;

    openai_v1_responses_complete_callback_t cb;
    void             *cb_arg;
} openai_v1_responses_sink_t;

/* -------------------------------------------------------------------------- */
/*  Helpers                                                                   */
/* -------------------------------------------------------------------------- */
static size_t write_cb(const void *ptr, size_t size, size_t nmemb,
                       curl_sink_interface_t *iface)
{
    openai_v1_responses_sink_t *o = (void *)iface;
    aml_buffer_append(o->response_buffer, ptr, size * nmemb);
    return size * nmemb;
}

static int get_int(ajson_t *node, const char *k, int d)
{
    return ajson_to_int(ajsono_scan(node, k), d);
}

/* -------------------------------------------------------------------------- */
static void on_complete(curl_sink_interface_t *iface,
                        curl_event_request_t    *req)
{
    openai_v1_responses_sink_t *o = (void *)iface;
    const char *raw = aml_buffer_data(o->response_buffer);
    // printf( "RAW:\n\n%s\n\n", raw);

    aml_pool_t *pool = iface->pool;
    ajson_t *json = ajson_parse_string(pool, raw);
    if (!json || ajson_is_error(json)) {
        fprintf(stderr, "[openai.responses.sink] JSON parse error\n");
        o->cb(o->cb_arg, req, false, NULL, -1, -1, -1);
        return;
    }

    /* output[0].content[0].text ------------------------------------------ */
    char *text = NULL;
    ajson_t *out_arr = ajsono_scan(json, "output");
    if (out_arr && ajson_is_array(out_arr) && ajsona_count(out_arr) > 0) {
        ajson_t *msg = ajsona_first(out_arr)->value;
        ajson_t *cont_arr = ajsono_scan(msg, "content");
        if (cont_arr && ajson_is_array(cont_arr) && ajsona_count(cont_arr) > 0) {
            ajson_t *first = ajsona_first(cont_arr)->value;
            text = ajsono_scan_strd(pool, first, "text", NULL);
        }
    }

    /* usage ---------------------------------------------------------------- */
    ajson_t *usage = ajsono_scan(json, "usage");
    int prompt_tokens     = get_int(usage, "input_tokens",  -1);
    int completion_tokens = get_int(usage, "output_tokens", -1);
    int total_tokens      = get_int(usage, "total_tokens",  -1);

    o->cb(o->cb_arg, req, true,
          text, prompt_tokens, completion_tokens, total_tokens);
}

static void on_failure(CURLcode res, long http,
                       curl_sink_interface_t *iface,
                       curl_event_request_t    *req)
{
    openai_v1_responses_sink_t *o = (void *)iface;
    fprintf(stderr, "[openai.responses.sink] HTTP %ld, CURL %d\n", http, res);
    o->cb(o->cb_arg, req, false, NULL, -1, -1, -1);
}

static bool init_sink(curl_sink_interface_t *iface, long /*len*/)
{
    openai_v1_responses_sink_t *o = (void *)iface;
    o->response_buffer = aml_buffer_init(2048);
    return o->response_buffer;
}

static void destroy_sink(curl_sink_interface_t *iface)
{
    openai_v1_responses_sink_t *o = (void *)iface;
    if (o->response_buffer) aml_buffer_destroy(o->response_buffer);
}

/* -------------------------------------------------------------------------- */
/*  Factory                                                                   */
/* -------------------------------------------------------------------------- */
curl_sink_interface_t *
openai_v1_responses_sink(curl_event_request_t *req,
                         openai_v1_responses_complete_callback_t cb,
                         void                                *cb_arg)
{
    openai_v1_responses_sink_t *o = aml_pool_zalloc(req->pool, sizeof(*o));
    if (!o) return NULL;

    o->cb     = cb;
    o->cb_arg = cb_arg;

    o->interface.pool     = req->pool;
    o->interface.init     = init_sink;
    o->interface.write    = write_cb;
    o->interface.complete = on_complete;
    o->interface.failure  = on_failure;
    o->interface.destroy  = destroy_sink;

    curl_event_request_sink(req, (curl_sink_interface_t *)o, NULL);

    return (curl_sink_interface_t *)o;
}
