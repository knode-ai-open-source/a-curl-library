// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcloud/google_embed.h"
#include "a-curl-library/plugins/gcloud/token.h"  /* gcloud_token_payload_t */
#include "a-curl-library/rate_manager.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_event_request.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GOOGLE_EMBEDDING_URL_FORMAT =
    "https://us-central1-aiplatform.googleapis.com/v1/projects/%s/locations/us-central1/publishers/google/models/%s:predict";

void curl_event_plugin_google_embed_set_rate(void) {
    /* name, bucket size, refill/sec — tune as needed for Vertex quotas */
    rate_manager_set_limit("google_embed", 50, 24.5);
}

/* Per-request context so we own buffers and know the token resource id. */
typedef struct google_embed_ctx_s {
    curl_event_res_id token_id;
    char *url;        /* owned */
    char *post_body;  /* owned */
} google_embed_ctx_t;

static void google_embed_ctx_destroy(void *userdata) {
    google_embed_ctx_t *ctx = (google_embed_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->url) aml_free(ctx->url);
    if (ctx->post_body) aml_free(ctx->post_body);
    aml_free(ctx);
}

/**
 * Prepare: read the token payload from the resource and set headers.
 */
static bool google_embed_on_prepare(curl_event_request_t *req) {
    google_embed_ctx_t *ctx = (google_embed_ctx_t *)req->userdata;

    const gcloud_token_payload_t *tok =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, ctx->token_id);
    if (!tok || !tok->access_token || !*tok->access_token) {
        fprintf(stderr, "[google_embed] Missing/invalid gcloud token payload.\n");
        return false;
    }

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", tok->access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    /* No need to modify URL (Vertex is HTTPS only; metadata flavor not used here). */
    return true;
}

curl_event_request_t *curl_event_plugin_google_embed_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *model_id,
    int output_dimensionality,
    curl_event_res_id  token_id,
    char **input_text,
    size_t num_texts,
    curl_output_interface_t *output_interface
) {
    if (!loop || !project_id || !model_id || token_id == 0 ||
        !input_text || num_texts == 0 || !output_interface) {
        fprintf(stderr, "[google_embed_init] Invalid arguments.\n");
        return NULL;
    }

    /* Build URL on heap. */
    char *url = aml_strdupf(GOOGLE_EMBEDDING_URL_FORMAT, project_id, model_id);
    if (!url) {
        fprintf(stderr, "[google_embed_init] OOM building URL.\n");
        return NULL;
    }

    /* Build JSON request using a pool, then duplicate to heap. */
    aml_pool_t *pool = aml_pool_init(16 * 1024);
    if (!pool) {
        aml_free(url);
        fprintf(stderr, "[google_embed_init] OOM creating pool.\n");
        return NULL;
    }

    ajson_t *root = ajsono(pool);

    /* parameters { outputDimensionality: N } */
    if (output_dimensionality > 0) {
        ajson_t *parameters = ajsono(pool);
        ajsono_append(parameters, "outputDimensionality",
                      ajson_number(pool, output_dimensionality), false);
        ajsono_append(root, "parameters", parameters, false);
    }

    /* instances: [{content: "..."}] */
    ajson_t *arr = ajsona(pool);
    for (size_t i = 0; i < num_texts; i++) {
        const char *s = input_text[i] ? input_text[i] : "";
        ajson_t *obj = ajsono(pool);
        ajsono_append(obj, "content", ajson_encode_str(pool, s), false);
        ajsona_append(arr, obj);
    }
    ajsono_append(root, "instances", arr, false);

    const char *json_tmp = ajson_stringify(pool, root);
    char *post_body = json_tmp ? aml_strdup(json_tmp) : NULL;
    aml_pool_destroy(pool);

    if (!post_body) {
        aml_free(url);
        fprintf(stderr, "[google_embed_init] OOM building JSON body.\n");
        return NULL;
    }

    /* Prepare per-request context */
    google_embed_ctx_t *ctx = (google_embed_ctx_t *)aml_calloc(1, sizeof(*ctx));
    if (!ctx) {
        aml_free(url);
        aml_free(post_body);
        fprintf(stderr, "[google_embed_init] OOM creating ctx.\n");
        return NULL;
    }
    ctx->token_id  = token_id;
    ctx->url       = url;
    ctx->post_body = post_body;

    /* Build the request */
    curl_event_request_t req = (curl_event_request_t){0};
    req.loop           = loop;
    req.url            = ctx->url;
    req.method         = "POST";
    req.post_data      = ctx->post_body;
    req.on_prepare     = google_embed_on_prepare;
    req.userdata       = ctx;
    req.userdata_cleanup = google_embed_ctx_destroy;

    /* Rate limiting / output / timeouts */
    req.rate_limit     = "google_embed";
    curl_output_defaults(&req, output_interface);

    req.low_speed_limit = 1024;    /* 1 KB/s */
    req.low_speed_time  = 15;
    req.max_retries     = 3;

    /* Depend on the gcloud token resource */
    curl_event_request_depend(&req, token_id);

    /* Enqueue */
    curl_event_request_t *r = curl_event_loop_enqueue(loop, &req, 0);
    if (!r) {
        google_embed_ctx_destroy(ctx);
        fprintf(stderr, "[google_embed_init] Failed to enqueue request.\n");
        return NULL;
    }
    return r;
}
