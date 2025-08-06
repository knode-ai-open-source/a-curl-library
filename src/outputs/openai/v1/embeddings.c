// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
//
// OpenAI “embeddings” output sink – parses the JSON blob and
// returns a `float **` array to the user callback.
#include "a-curl-library/outputs/openai/v1/embeddings.h"

#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_buffer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Internal sink object ---------------------------------------------------- */
typedef struct openai_v1_embeddings_sink_s {
    curl_output_interface_t interface;
    aml_pool_t    *pool;
    aml_buffer_t  *response_buffer;
    openai_v1_embeddings_complete_callback_t complete_callback;
    void          *complete_callback_arg;
    size_t         expected_embedding_size;
} openai_v1_embeddings_sink_t;

/* Write – just buffer ----------------------------------------------------- */
static size_t sink_write(const void *data, size_t size, size_t nmemb,
                         curl_output_interface_t *iface)
{
    openai_v1_embeddings_sink_t *s = (openai_v1_embeddings_sink_t *)iface;
    aml_buffer_append(s->response_buffer, data, size * nmemb);
    return size * nmemb;
}

/* Failure ----------------------------------------------------------------- */
static void sink_failure(CURLcode res, long http,
                         curl_output_interface_t *iface,
                         curl_event_request_t *req)
{
    openai_v1_embeddings_sink_t *s = (openai_v1_embeddings_sink_t *)iface;
    fprintf(stderr, "[openai_v1_embeddings_output] HTTP %ld, CURL %d\n", http, res);
    s->complete_callback(s->complete_callback_arg, req, false, NULL, 0, 0);
}

/* Init / destroy ---------------------------------------------------------- */
static bool sink_init(curl_output_interface_t *iface, long _len)
{
    (void)_len;
    openai_v1_embeddings_sink_t *s = (openai_v1_embeddings_sink_t *)iface;
    s->pool            = aml_pool_init(4096);
    s->response_buffer = aml_buffer_init(1024);
    return true;
}
static void sink_destroy(curl_output_interface_t *iface)
{
    openai_v1_embeddings_sink_t *s = (openai_v1_embeddings_sink_t *)iface;
    aml_pool_destroy(s->pool);
    aml_buffer_destroy(s->response_buffer);
    aml_free(s);
}

/* Complete – parse embeddings -------------------------------------------- */
static void sink_complete(curl_output_interface_t *iface,
                          curl_event_request_t *req)
{
    openai_v1_embeddings_sink_t *s = (openai_v1_embeddings_sink_t *)iface;

    ajson_t *json = ajson_parse_string(
        s->pool, aml_buffer_data(s->response_buffer));
    aml_buffer_clear(s->response_buffer);

    if (!json || ajson_is_error(json)) {
        fprintf(stderr, "[openai_v1_embeddings_output] JSON parse error\n");
        s->complete_callback(s->complete_callback_arg, req, false, NULL, 0, 0);
        return;
    }

    ajson_t *data = ajsono_scan(json, "data");
    if (!data || !ajson_is_array(data)) {
        fprintf(stderr, "[openai_v1_embeddings_output] No embeddings array\n");
        s->complete_callback(s->complete_callback_arg, req, false, NULL, 0, 0);
        return;
    }

    size_t n_emb = ajsona_count(data);
    float **vecs = aml_pool_alloc(s->pool, n_emb * sizeof(float *));
    if (!vecs) {
        s->complete_callback(s->complete_callback_arg, req, false, NULL, 0, 0);
        return;
    }

    size_t idx = 0;
    for (ajsona_t *el = ajsona_first(data); el && idx < n_emb;
         el = ajsona_next(el), ++idx)
    {
        size_t dim = 0;
        vecs[idx] = ajson_extract_float_array(
            &dim, s->pool, ajsono_scan(el->value, "embedding"));

        if (s->expected_embedding_size && dim != s->expected_embedding_size) {
            fprintf(stderr,
                    "[openai_v1_embeddings_output] Unexpected dim %zu vs exp %zu\n",
                    dim, s->expected_embedding_size);
            s->complete_callback(s->complete_callback_arg, req, false, NULL, 0, 0);
            return;
        }
    }

    s->complete_callback(s->complete_callback_arg, req, true,
                         vecs, n_emb, s->expected_embedding_size);
}

/* Factory ----------------------------------------------------------------- */
curl_output_interface_t *openai_v1_embeddings_output(
    size_t expected_dim,
    openai_v1_embeddings_complete_callback_t cb,
    void *cb_arg)
{
    openai_v1_embeddings_sink_t *s = aml_calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->expected_embedding_size = expected_dim;
    s->complete_callback       = cb;
    s->complete_callback_arg   = cb_arg;

    s->interface.init     = sink_init;
    s->interface.write    = sink_write;
    s->interface.failure  = sink_failure;
    s->interface.complete = sink_complete;
    s->interface.destroy  = sink_destroy;

    return (curl_output_interface_t *)s;
}
