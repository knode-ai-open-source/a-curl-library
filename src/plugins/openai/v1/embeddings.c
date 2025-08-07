// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
//
// ──────────────────────────────────────────────────────────────────────────────
//  OpenAI – “/v1/embeddings” request builder
//      • Per-request state kept in req->plugin_data
// ──────────────────────────────────────────────────────────────────────────────
#include "a-curl-library/plugins/openai/v1/embeddings.h"

#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"

#include <stdio.h>
#include <string.h>

static const char *URL = "https://api.openai.com/v1/embeddings";

/* -------------------------------------------------------------------------- */
/*  plugin-data block                                                         */
/* -------------------------------------------------------------------------- */
typedef struct openai_v1_embeddings_pd_s {
    curl_event_res_id api_key_id;  /* dependency providing the Bearer token   */
    ajson_t          *inputs;      /* persistent "input": [] array node       */
} openai_v1_embeddings_pd_t;

#define PD(req) ((openai_v1_embeddings_pd_t *)(req)->plugin_data)

/* -------------------------------------------------------------------------- */
/*  on_prepare – inject Authorization header                                  */
/* -------------------------------------------------------------------------- */
static bool _on_prepare(curl_event_request_t *req)
{
    if (!req || !req->plugin_data) return false;
    openai_v1_embeddings_pd_t *pd = PD(req);

    const char *key = (const char *)
        curl_event_res_peek(req->loop, pd->api_key_id);
    if (!key || !*key) {
        fprintf(stderr, "[openai.embed] missing API key\n");
        return false;
    }

    char hdr[1024];
    snprintf(hdr, sizeof(hdr), "Bearer %s", key);
    curl_event_request_set_header(req, "Authorization", hdr);

    /* Content-Type is handled automatically when the JSON body is committed. */
    return true;
}

/* -------------------------------------------------------------------------- */
/*  Builder                                                                    */
/* -------------------------------------------------------------------------- */
curl_event_request_t *
openai_v1_embeddings_new(curl_event_loop_t       *loop,
                 curl_event_res_id        api_key_id,
                 const char              *model_id)
{
    if (!loop || api_key_id == 0 || !model_id || !*model_id) {
        fprintf(stderr, "[openai.embed] invalid args\n");
        return NULL;
    }

    /* -------- allocate request ------------------------------------------ */
    curl_event_request_t *req = curl_event_request_new(0);
    if (!req) return NULL;

    curl_event_request_url(req, URL);
    curl_event_request_method(req, "POST");

    /* -------- allocate plugin-data in request pool ----------------------- */
    openai_v1_embeddings_pd_t *pd = (openai_v1_embeddings_pd_t *)
        aml_pool_calloc(req->pool, 1, sizeof(*pd));
    pd->api_key_id = api_key_id;
    req->plugin_data = pd;
    req->plugin_data_cleanup = NULL; /* pool-backed – no explicit free */

    /* -------- baseline behaviour ---------------------------------------- */
    curl_event_request_depend(req, api_key_id);
    curl_event_request_set_header(req, "Accept", "application/json");
    curl_event_request_low_speed(req, 1024, 60);
    curl_event_request_enable_retries(req, 3, 2.0, 250, 20000, true);
    curl_event_request_on_prepare(req, _on_prepare);

    /* -------- JSON root -------------------------------------------------- */
    ajson_t *root = curl_event_request_json_begin(req, /*array_root=*/false);
    ajsono_append(root, "model",
                  ajson_encode_str(req->pool, model_id), false);
    ajsono_append(root, "encoding_format",
                  ajson_encode_str(req->pool, "float"), false);

    pd->inputs = ajsona(req->pool);          /* initialise [] and stash ptr */
    ajsono_append(root, "input", pd->inputs, false);

    return req;
}

/* -------------------------------------------------------------------------- */
/*  Helpers                                                                    */
/* -------------------------------------------------------------------------- */
void openai_v1_embeddings_add_text(curl_event_request_t *req, const char *text)
{
    if (!req || !text) return;
    openai_v1_embeddings_pd_t *pd = PD(req);
    if (!pd) return;

    /* Ensure input array exists (might’ve been cleared by caller) */
    if (!pd->inputs) {
        ajson_t *root = curl_event_request_json_begin(req, false);
        pd->inputs = ajsona(req->pool);
        ajsono_append(root, "input", pd->inputs, false);
    }
    ajsona_append(pd->inputs, ajson_encode_str(req->pool, text));
}

void openai_v1_embeddings_add_texts(curl_event_request_t *req,
                            const char **texts, size_t n)
{
    if (!req || !texts || n == 0) return;
    for (size_t i = 0; i < n; ++i)
        if (texts[i]) openai_v1_embeddings_add_text(req, texts[i]);
}

void openai_v1_embeddings_set_dimensions(curl_event_request_t *req, int dimensions)
{
    if (!req || dimensions <= 0) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "dimensions",
                  ajson_number(req->pool, dimensions), false);
}

void openai_v1_embeddings_set_encoding_format(curl_event_request_t *req,
                                      const char *fmt)
{
    if (!req || !fmt || !*fmt) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "encoding_format",
                  ajson_encode_str(req->pool, fmt), false);
}

void openai_v1_embeddings_set_user(curl_event_request_t *req, const char *user)
{
    if (!req || !user) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "user",
                  ajson_encode_str(req->pool, user), false);
}

void openai_v1_embeddings_add_dependency(curl_event_request_t *req,
                                 curl_event_res_id     dep_res)
{
    if (!req || !dep_res) return;
    curl_event_request_depend(req, dep_res);
}
