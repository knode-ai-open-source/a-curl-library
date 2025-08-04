// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/plugins/openai/embed.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <string.h>

static const char *URL = "https://api.openai.com/v1/embeddings";

typedef struct {
    curl_event_res_id api_key_id;
    ajson_t          *inputs;  /* persistent "input" array */
} openai_embed_ud_t;

/* Prepare: set Authorization from API key resource. */
static bool _on_prepare(curl_event_request_t *req) {
    if (!req || !req->output_data) return false;
    openai_embed_ud_t *ud = (openai_embed_ud_t *)req->output_data;

    const char *key = (const char *)curl_event_res_peek(req->loop, ud->api_key_id);
    if (!key || !*key) {
        fprintf(stderr, "[openai.embed] missing API key\n");
        return false;
    }
    char hdr[1024];
    snprintf(hdr, sizeof(hdr), "Bearer %s", key);
    curl_event_request_set_header(req, "Authorization", hdr);
    /* Content-Type is set automatically when JSON commits. */
    return true;
}

curl_event_request_t *
openai_embed_new(curl_event_loop_t       *loop,
                 curl_event_res_id        api_key_id,
                 const char              *model_id,
                 curl_output_interface_t *output_iface)
{
    if (!loop || api_key_id == 0 || !model_id || !*model_id || !output_iface) {
        fprintf(stderr, "[openai.embed] invalid args\n");
        return NULL;
    }

    curl_event_request_t *req = curl_event_request_new(0);
    if (!req) return NULL;

    curl_event_request_url(req, URL);
    curl_event_request_method(req, "POST");
    curl_output_defaults(req, output_iface);

    curl_event_request_set_header(req, "Accept", "application/json");
    curl_event_request_low_speed(req, 1024, 60);
    curl_event_request_enable_retries(req, 3, 2.0, 250, 20000, true);

    /* deps + output_data */
    curl_event_request_depend(req, api_key_id);
    openai_embed_ud_t *ud = (openai_embed_ud_t *)aml_pool_calloc(req->pool, 1, sizeof(*ud));
    ud->api_key_id = api_key_id;
    req->output_data = ud;
    req->output_data_cleanup = NULL;

    curl_event_request_on_prepare(req, _on_prepare);

    /* JSON: model + default encoding_format + empty input array */
    ajson_t *root = curl_event_request_json_begin(req, /*array_root=*/false);
    ajsono_append(root, "model", ajson_encode_str(req->pool, model_id), false);
    ajsono_append(root, "encoding_format", ajson_encode_str(req->pool, "float"), false);

    ud->inputs = ajsona(req->pool);
    ajsono_append(root, "input", ud->inputs, false);

    return req;
}

void openai_embed_add_text(curl_event_request_t *req, const char *text) {
    if (!req || !text) return;
    openai_embed_ud_t *ud = (openai_embed_ud_t *)req->output_data;
    ajson_t *root = curl_event_request_json_begin(req, false);
    if (!ud->inputs) {
        ud->inputs = ajsona(req->pool);
        ajsono_append(root, "input", ud->inputs, false);
    }
    ajsona_append(ud->inputs, ajson_encode_str(req->pool, text));
}

void openai_embed_add_texts(curl_event_request_t *req,
                            const char **texts, size_t n)
{
    if (!req || !texts || n == 0) return;
    for (size_t i = 0; i < n; ++i) {
        if (texts[i]) openai_embed_add_text(req, texts[i]);
    }
}

void openai_embed_set_dimensions(curl_event_request_t *req, int dimensions) {
    if (!req || dimensions <= 0) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "dimensions", ajson_number(req->pool, dimensions), false);
}

void openai_embed_set_encoding_format(curl_event_request_t *req, const char *fmt) {
    if (!req || !fmt || !*fmt) return;
    /* Allow "float" or "base64" (OpenAI). Caller may use others at their own risk. */
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "encoding_format", ajson_encode_str(req->pool, fmt), false);
}

void openai_embed_set_user(curl_event_request_t *req, const char *user) {
    if (!req || !user) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "user", ajson_encode_str(req->pool, user), false);
}

void openai_embed_add_dependency(curl_event_request_t *req, curl_event_res_id dep_res) {
    if (!req || !dep_res) return;
    curl_event_request_depend(req, dep_res);
}
