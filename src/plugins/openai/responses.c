// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
//
// ──────────────────────────────────────────────────────────────────────────────
//  OpenAI - “/v1/responses” helper
//
//  ▸ Keeps the curl-output sink object in req->output_data (mandatory!)
//  ▸ Stashes extra per-request state *inside* that same sink
//  ▸ No more double-use of req->output_data → crash resolved
// ──────────────────────────────────────────────────────────────────────────────
#include "a-curl-library/plugins/openai/responses.h"

#include "a-curl-library/outputs/openai/responses.h"   // sink interface
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  internal config                                                           */
/* -------------------------------------------------------------------------- */
static const char *URL = "https://api.openai.com/v1/responses";

/* -------------------------------------------------------------------------- */
/*  Sink structure — **must** start with curl_output_interface_t              */
/*  (duplicated from  src/outputs/openai/responses.c  so we can access fields) */
/* -------------------------------------------------------------------------- */
typedef struct openai_responses_output_s {
    /* ― first field: required by curl_output_defaults ― */
    curl_output_interface_t interface;

    /* --- per-request state shared with this file ------------------------- */
    curl_event_res_id api_key_id;
    curl_event_res_id prev_id_res;      /* 0 if none                         */
    ajson_t          *msg_array;        /* used when building messages input */

    /* --- private fields used by the sink implementation ------------------ */
    aml_pool_t   *pool;
    aml_buffer_t *response_buffer;
    openai_responses_complete_callback_t cb;
    void         *cb_arg;
} openai_responses_output_t;

/* convenience macro */
#define SINK(req) ((openai_responses_output_t *)(req)->output_data)

/* -------------------------------------------------------------------------- */
/*  on_prepare — inject Authorization header + previous_response_id           */
/* -------------------------------------------------------------------------- */
static bool _on_prepare(curl_event_request_t *req)
{
    if (!req || !req->output_data) return false;
    openai_responses_output_t *sink = SINK(req);

    /* -- Bearer token ------------------------------------------------------ */
    const char *key = (const char *)curl_event_res_peek(req->loop,
                                                       sink->api_key_id);
    if (!key || !*key) {
        fprintf(stderr, "[openai.responses] missing API key\n");
        return false;
    }
    char hdr[1024];
    snprintf(hdr, sizeof(hdr), "Bearer %s", key);
    curl_event_request_set_header(req, "Authorization", hdr);

    /* -- previous_response_id (optional) ----------------------------------- */
    if (sink->prev_id_res) {
        const char *prev = (const char *)
            curl_event_res_peek(req->loop, sink->prev_id_res);
        if (prev && *prev) {
            ajson_t *root = curl_event_request_json_begin(req, false);
            ajsono_append(root, "previous_response_id",
                          ajson_encode_str(req->pool, prev), /*encoded=*/false);
        }
    }
    return true;  /* JSON commit handled later by core */
}

/* -------------------------------------------------------------------------- */
/*  Builder                                                                   */
/* -------------------------------------------------------------------------- */
curl_event_request_t *
openai_responses_new(curl_event_loop_t       *loop,
                     curl_event_res_id        api_key_id,
                     const char              *model_id,
                     curl_output_interface_t *output_iface)
{
    if (!loop || api_key_id == 0 || !model_id || !*model_id || !output_iface) {
        fprintf(stderr, "[openai.responses] invalid args\n");
        return NULL;
    }

    /* ---------- create basic request ------------------------------------- */
    curl_event_request_t *req = curl_event_request_new(0);
    if (!req) return NULL;

    curl_event_request_url(req, URL);
    curl_event_request_method(req, "POST");

    /* ---------- wire default output sink --------------------------------- */
    curl_output_defaults(req, output_iface);

    /* ---------- initialise shared sink fields ---------------------------- */
    openai_responses_output_t *sink = (openai_responses_output_t *)output_iface;
    sink->api_key_id  = api_key_id;
    sink->prev_id_res = 0;
    sink->msg_array   = NULL;

    /* ---------- dependency on API-key resource --------------------------- */
    curl_event_request_depend(req, api_key_id);

    /* ---------- on_prepare callback -------------------------------------- */
    curl_event_request_on_prepare(req, _on_prepare);

    /* ---------- sensible defaults ---------------------------------------- */
    curl_event_request_low_speed(req,         1024, 60);
    curl_event_request_enable_retries(req,    3, 2.0, 250, 20000, true);
    curl_event_request_set_header(req, "Accept", "application/json");

    /* ---------- initial JSON payload ------------------------------------- */
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "model",
                  ajson_encode_str(req->pool, model_id), false);

    return req;
}

/* -------------------------------------------------------------------------- */
/*  Parameter helpers                                                         */
/* -------------------------------------------------------------------------- */
void openai_responses_set_temperature(curl_event_request_t *req, float t)
{
    if (!req || t < 0) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "temperature",
                  ajson_number_stringf(req->pool, "%0.2f", t), false);
}

void openai_responses_set_max_output_tokens(curl_event_request_t *req, int n)
{
    if (!req || n <= 0) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "max_output_tokens", ajson_number(req->pool, n), false);
}

void openai_responses_set_instructions(curl_event_request_t *req,
                                       const char *s)
{
    if (!req || !s) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "instructions",
                  ajson_encode_str(req->pool, s), false);
}

/* -------------------------------------------------------------------------- */
/*  Inputs                                                                    */
/* -------------------------------------------------------------------------- */
void openai_responses_input_text(curl_event_request_t *req,
                                 const char *text)
{
    if (!req || !text) return;
    SINK(req)->msg_array = NULL;                    /* switch to text mode  */
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "input",
                  ajson_encode_str(req->pool, text), false);
}

void openai_responses_add_message(curl_event_request_t *req,
                                  const char *role,
                                  const char *content)
{
    if (!req) return;
    if (!role)    role    = "user";
    if (!content) content = "";

    openai_responses_output_t *sink = SINK(req);
    ajson_t *root = curl_event_request_json_begin(req, false);

    if (!sink->msg_array) {                        /* first time → make [] */
        sink->msg_array = ajsona(req->pool);
        ajsono_append(root, "input", sink->msg_array, false);
    }

    ajson_t *m = ajsono(req->pool);
    ajsono_append(m, "role",
                  ajson_encode_str(req->pool, role), false);
    ajsono_append(m, "content",
                  ajson_encode_str(req->pool, content), false);
    ajsona_append(sink->msg_array, m);
}

void openai_responses_set_prompt(curl_event_request_t *req,
                                 const char *id,
                                 const char *version)
{
    if (!req || !id) return;
    SINK(req)->msg_array = NULL;                   /* switch to prompt mode */

    ajson_t *root = curl_event_request_json_begin(req, false);
    ajson_t *p    = ajsono(req->pool);
    ajsono_append(p, "id",
                  ajson_encode_str(req->pool, id), false);
    if (version)
        ajsono_append(p, "version",
                      ajson_encode_str(req->pool, version), false);
    ajsono_append(root, "prompt", p, false);
}

/* -------------------------------------------------------------------------- */
/*  Chaining + extra dependencies                                             */
/* -------------------------------------------------------------------------- */
void openai_responses_chain_previous_response(curl_event_request_t *req,
                                              curl_event_res_id      prev_id_res)
{
    if (!req || !prev_id_res) return;
    openai_responses_output_t *sink = SINK(req);
    sink->prev_id_res = prev_id_res;
    curl_event_request_depend(req, prev_id_res);
}

void openai_responses_add_dependency(curl_event_request_t *req,
                                     curl_event_res_id     dep_res)
{
    if (!req || !dep_res) return;
    curl_event_request_depend(req, dep_res);
}
