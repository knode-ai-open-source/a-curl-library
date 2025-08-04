// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-library/outputs/gcloud/embed.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Internal output object (duplicated in openai file for simplicity) */
typedef struct {
    curl_output_interface_t interface;
    aml_pool_t *pool;
    aml_buffer_t *response_buffer;
    gcloud_embedding_complete_callback_t complete_callback;
    void *complete_callback_arg;
    size_t expected_embedding_size;
} embedding_output_t;

/* write → buffer the whole response body */
static size_t embedding_write_callback(const void *data, size_t size, size_t nmemb,
                                       curl_output_interface_t *interface) {
    embedding_output_t *output = (embedding_output_t *)interface;
    size_t total = size * nmemb;
    aml_buffer_append(output->response_buffer, data, total);
    return total;
}

/* failure → bubble up as false */
static void embedding_on_failure(CURLcode result, long http_code,
                                 curl_output_interface_t *userdata,
                                 curl_event_request_t *req) {
    embedding_output_t *output = (embedding_output_t *)userdata;
    fprintf(stderr, "[google_embed_output] failure HTTP %ld, CURL %d\n", http_code, result);
    output->complete_callback(output->complete_callback_arg, req, false, NULL, 0, 0);
}

/* init/destroy */
static bool embedding_init(curl_output_interface_t *interface, long content_length) {
    (void)content_length;
    embedding_output_t *output = (embedding_output_t *)interface;
    output->pool = aml_pool_init(4096);
    output->response_buffer = aml_buffer_init(1024);
    return true;
}

static void embedding_destroy(curl_output_interface_t *interface) {
    embedding_output_t *output = (embedding_output_t *)interface;
    if (output->pool) aml_pool_destroy(output->pool);
    if (output->response_buffer) aml_buffer_destroy(output->response_buffer);
    aml_free(output);
}

/* Google (Vertex AI) specific parse */
static void parse_google_embeddings(curl_output_interface_t *userdata,
                                    curl_event_request_t *req) {
    embedding_output_t *output = (embedding_output_t *)userdata;

    /* Optional: log raw for debugging */
    // fprintf(stderr, "%s\n", aml_buffer_data(output->response_buffer));

    ajson_t *json = ajson_parse_string(output->pool, aml_buffer_data(output->response_buffer));
    aml_buffer_clear(output->response_buffer);

    if (!json || ajson_is_error(json)) {
        fprintf(stderr, "[parse_google_embeddings] Failed to parse JSON response.\n");
        output->complete_callback(output->complete_callback_arg, req, false, NULL, 0, 0);
        return;
    }

    ajson_t *predictions = ajsono_scan(json, "predictions");
    if (!predictions || !ajson_is_array(predictions)) {
        fprintf(stderr, "[parse_google_embeddings] No embeddings found.\n");
        output->complete_callback(output->complete_callback_arg, req, false, NULL, 0, 0);
        return;
    }

    size_t num_embeddings = ajsona_count(predictions);
    float **embeddings = aml_pool_alloc(output->pool, num_embeddings * sizeof(float *));
    if (!embeddings) {
        fprintf(stderr, "[parse_google_embeddings] Memory allocation failed.\n");
        output->complete_callback(output->complete_callback_arg, req, false, NULL, 0, 0);
        return;
    }

    size_t i = 0;
    for (ajsona_t *node = ajsona_first(predictions); node && i < num_embeddings; node = ajsona_next(node)) {
        ajson_t *emb_obj = ajsono_scan(node->value, "embeddings");
        if (!emb_obj || !ajson_is_object(emb_obj)) {
            fprintf(stderr, "[parse_google_embeddings] Missing 'embeddings' object.\n");
            output->complete_callback(output->complete_callback_arg, req, false, NULL, 0, 0);
            return;
        }

        size_t dim = 0;
        embeddings[i] = ajson_extract_float_array(&dim, output->pool, ajsono_scan(emb_obj, "values"));
        if (dim != output->expected_embedding_size) {
            fprintf(stderr, "[parse_google_embeddings] Unexpected embedding size: expected %zu, got %zu\n",
                    output->expected_embedding_size, dim);
            output->complete_callback(output->complete_callback_arg, req, false, NULL, 0, 0);
            return;
        }
        i++;
    }

    output->complete_callback(output->complete_callback_arg, req, true,
                              embeddings, num_embeddings, output->expected_embedding_size);
}

/* Factory */
curl_output_interface_t *google_embed_output(
    size_t expected_embedding_size,
    gcloud_embedding_complete_callback_t complete_callback,
    void *complete_callback_arg)
{
    embedding_output_t *output = aml_calloc(1, sizeof(*output));
    if (!output) return NULL;

    output->expected_embedding_size = expected_embedding_size;
    output->complete_callback = complete_callback;
    output->complete_callback_arg = complete_callback_arg;

    output->interface.init = embedding_init;
    output->interface.write = embedding_write_callback;
    output->interface.failure = embedding_on_failure;
    output->interface.complete = parse_google_embeddings;
    output->interface.destroy = embedding_destroy;

    return (curl_output_interface_t *)output;
}
