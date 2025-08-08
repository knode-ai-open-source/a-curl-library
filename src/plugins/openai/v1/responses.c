// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
//
// OpenAI “/v1/responses” request builder
// ─────────────────────────────────────────────────────────────────────────────
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Endpoint                                                                  */
/* -------------------------------------------------------------------------- */
static const char *URL = "https://api.openai.com/v1/responses";

/* -------------------------------------------------------------------------- */
/*  Per-request plugin-data record                                            */
/* -------------------------------------------------------------------------- */
typedef struct {
    curl_event_res_id api_key_id;      /* dependency providing the key      */
    curl_event_res_id prev_id_res;     /* 0 if none                         */
    ajson_t          *msg_array;       /* for messages-style input          */
} openai_v1_responses_pd_t;

#define PD(req)  ((openai_v1_responses_pd_t *)(req)->plugin_data)

/* -------------------------------------------------------------------------- */
/*  on_prepare: add Authorization + previous_response_id                      */
/* -------------------------------------------------------------------------- */
static bool _on_prepare(curl_event_request_t *req)
{
    if (!req || !req->plugin_data) return false;
    openai_v1_responses_pd_t *pd = PD(req);

    /* Bearer token --------------------------------------------------------- */
    const char *key = (const char *)curl_event_res_peek(req->loop,
                                                        pd->api_key_id);
    if (!key || !*key) {
        fprintf(stderr, "[openai.responses] missing API key\n");
        return false;
    }
    char hdr[1024];
    snprintf(hdr, sizeof(hdr), "Bearer %s", key);
    curl_event_request_set_header(req, "Authorization", hdr);

    /* previous_response_id -------------------------------------------------- */
    if (pd->prev_id_res) {
        const char *prev = (const char *)
            curl_event_res_peek(req->loop, pd->prev_id_res);
        if (prev && *prev) {
            ajson_t *root = curl_event_request_json_begin(req, false);
            ajsono_append(root, "previous_response_id",
                          ajson_encode_str(req->pool, prev), false);
        }
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/*  Builder                                                                   */
/* -------------------------------------------------------------------------- */
curl_event_request_t *
openai_v1_responses_init(curl_event_loop_t       *loop,
                     curl_event_res_id        api_key_id,
                     const char              *model_id)
{
    if (!loop || !api_key_id || !model_id || !*model_id) {
        fprintf(stderr, "[openai.responses] invalid args\n");
        return NULL;
    }

    /* ------------------------------------------------------------------ */
    /*  Allocate request                                                  */
    /* ------------------------------------------------------------------ */
    curl_event_request_t *req = curl_event_request_init(0);
    if (!req) return NULL;

    curl_event_request_url   (req, URL);
    curl_event_request_method(req, "POST");

    /* ------------------------------------------------------------------ */
    /*  Plugin data                                                       */
    /* ------------------------------------------------------------------ */
    openai_v1_responses_pd_t *pd = aml_pool_calloc(req->pool, 1, sizeof(*pd));
    pd->api_key_id  = api_key_id;
    pd->prev_id_res = 0;
    pd->msg_array   = NULL;
    curl_event_request_plugin_data(req, pd, /*cleanup=*/NULL);

    /* ------------------------------------------------------------------ */
    /*  Dependencies / callbacks / defaults                               */
    /* ------------------------------------------------------------------ */
    curl_event_request_depend(req, api_key_id);
    curl_event_request_on_prepare(req, _on_prepare);

    curl_event_request_low_speed       (req, 1024, 60);
    curl_event_request_enable_retries  (req, 3, 2.0, 250, 20000, true);
    curl_event_request_set_header      (req, "Accept", "application/json");

    /* JSON payload seed ------------------------------------------------ */
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "model",
                  ajson_encode_str(req->pool, model_id), false);

    return req;
}

/* Enable Structured Outputs */
void openai_v1_responses_set_structured_output_json(
    curl_event_request_t *req,
    const char *name,        /* e.g. "ideas" */
    ajson_t *json,
    bool        strict);     /* true ⇒ add "strict":true */
{
    aml_pool_t *p   = req->pool;
    ajson_t   *root = curl_event_request_json_root(req);

    ajson_t *text = ajsono_scan(root,"text");
    if (!text) { text = ajsono(p); ajsono_append(root,"text",text,false); }

    ajson_t *fmt = ajsono(p);
    ajsono_append(fmt,"name",   ajson_str(p,name),false);
    ajsono_append(fmt,"type",   ajson_str(p,"json_schema"),false);
    ajsono_append(fmt,"schema", json,false);
    if (strict) ajsono_append(fmt,"strict", ajson_true(p),false);

    ajsono_append(text,"format",fmt,false);
}

void openai_v1_responses_set_structured_output(curl_event_request_t *req,
                                               const char *name,        /* e.g. "ideas" */
                                               const char *schema_json, /* raw schema */
                                               bool strict)             /* true ⇒ "strict":true */
{
    openai_v1_responses_set_structured_output_json(req, name, ajson_parse_string(req->pool,schema_json), strict);
}

/* -------------------------------------------------------------------------- */
/*  Parameter helpers                                                         */
/* -------------------------------------------------------------------------- */
void openai_v1_responses_set_temperature(curl_event_request_t *req, float t)
{
    if (!req || t < 0) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "temperature",
                  ajson_number_stringf(req->pool, "%0.2f", t), false);
}

void openai_v1_responses_set_max_output_tokens(curl_event_request_t *req, int n)
{
    if (!req || n <= 0) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "max_output_tokens",
                  ajson_number(req->pool, n), false);
}

void openai_v1_responses_set_instructions(curl_event_request_t *req,
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
void openai_v1_responses_input_text(curl_event_request_t *req,
                                 const char *text)
{
    if (!req || !text) return;
    PD(req)->msg_array = NULL;     /* switch to text mode */
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "input",
                  ajson_encode_str(req->pool, text), false);
}

void openai_v1_responses_add_message(curl_event_request_t *req,
                                  const char *role,
                                  const char *content)
{
    if (!req) return;
    if (!role)    role    = "user";
    if (!content) content = "";

    openai_v1_responses_pd_t *pd = PD(req);
    ajson_t *root = curl_event_request_json_begin(req, false);

    if (!pd->msg_array) {
        pd->msg_array = ajsona(req->pool);
        ajsono_append(root, "input", pd->msg_array, false);
    }

    ajson_t *m = ajsono(req->pool);
    ajsono_append(m, "role",
                  ajson_encode_str(req->pool, role), false);
    ajsono_append(m, "content",
                  ajson_encode_str(req->pool, content), false);
    ajsona_append(pd->msg_array, m);
}

void openai_v1_responses_set_prompt(curl_event_request_t *req,
                                 const char *id,
                                 const char *version)
{
    if (!req || !id) return;
    PD(req)->msg_array = NULL;  /* switch to prompt mode */

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
/*  Chaining + extra deps                                                     */
/* -------------------------------------------------------------------------- */
void openai_v1_responses_chain_previous_response(curl_event_request_t *req,
                                              curl_event_res_id      prev_id_res)
{
    if (!req || !prev_id_res) return;
    PD(req)->prev_id_res = prev_id_res;
    curl_event_request_depend(req, prev_id_res);
}

void openai_v1_responses_add_dependency(curl_event_request_t *req,
                                     curl_event_res_id     dep_res)
{
    if (!req || !dep_res) return;
    curl_event_request_depend(req, dep_res);
}

/* ---------- internal: ensure we have an include array ---------- */
static ajson_t *ensure_include_array(curl_event_request_t *req) {
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajson_t *inc  = ajsono_scan(root, "include");
    if (!inc || !ajson_is_array(inc)) {
        inc = ajsona(req->pool);
        /* Replace if present, append if missing */
        ajsono_set(root, "include", inc, false);
    }
    return inc;
}

/* ---------- internal: dedupe check (linear; fine for tiny lists) ---------- */
static bool include_contains(ajson_t *inc, const char *value) {
    if (!inc || !ajson_is_array(inc) || !value) return false;
    int n = ajsona_count(inc);
    for (int i = 0; i < n; ++i) {
        const char *s = ajson_to_str(ajsona_nth(inc, i), "");
        if (s && strcmp(s, value) == 0) return true;
    }
    return false;
}

/* ---------- public: add / clear / set ---------- */
void openai_v1_responses_add_include(curl_event_request_t *req,
                                     const char *value)
{
    if (!req || !value || !*value) return;
    ajson_t *inc = ensure_include_array(req);
    if (!include_contains(inc, value)) {
        ajsona_append(inc, ajson_str(req->pool, value));
    }
}

void openai_v1_responses_clear_includes(curl_event_request_t *req)
{
    if (!req) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    /* simplest: drop the key entirely; next add will recreate it */
    ajsono_remove(root, "include");
}

void openai_v1_responses_set_includes(curl_event_request_t *req,
                                      const char *const *items, size_t count)
{
    if (!req) return;
    ajson_t *root = curl_event_request_json_begin(req, false);

    /* Build a fresh array (optionally dedupe as we go) */
    ajson_t *arr = ajsona(req->pool);
    if (items) {
        for (size_t i = 0; i < count; ++i) {
            const char *v = items[i];
            if (v && *v && !include_contains(arr, v)) {
                ajsona_append(arr, ajson_str(req->pool, v));
            }
        }
    }
    /* Replace existing or append new */
    ajsono_set(root, "include", arr, false);
}

/* ---------- presets (unchanged externally) ---------- */
void openai_v1_responses_include_stream_text_minimal(curl_event_request_t *req)
{
    if (!req) return;
    openai_v1_responses_set_stream(req, true);
    const char *inc[] = {
        OPENAI_INC_STREAM_TEXT_DELTA,
        OPENAI_INC_STREAM_TEXT_DONE
    };
    openai_v1_responses_set_includes(req, inc, sizeof inc / sizeof inc[0]);
}

void openai_v1_responses_include_stream_text_and_tools(curl_event_request_t *req)
{
    if (!req) return;
    openai_v1_responses_set_stream(req, true);
    const char *inc[] = {
        OPENAI_INC_STREAM_TEXT_DELTA,
        OPENAI_INC_STREAM_TEXT_DONE,
        OPENAI_INC_FUNC_ARGS_DELTA,
        OPENAI_INC_FUNC_ARGS_DONE
    };
    openai_v1_responses_set_includes(req, inc, sizeof inc / sizeof inc[0]);
}

void openai_v1_responses_include_input_image_urls(curl_event_request_t *req)
{
    if (!req) return;
    openai_v1_responses_add_include(req, OPENAI_INC_INPUT_IMAGE_URL);
}

void openai_v1_responses_include_reasoning_encrypted(curl_event_request_t *req)
{
    if (!req) return;
    openai_v1_responses_add_include(req, OPENAI_INC_REASONING_BLOB);
}

void openai_v1_responses_include_refusal(curl_event_request_t *req)
{
    if (!req) return;
    openai_v1_responses_add_include(req, OPENAI_INC_REFUSAL_ANY);
}

void openai_v1_responses_include_debug(curl_event_request_t *req)
{
    if (!req) return;
    openai_v1_responses_set_stream(req, true);
    const char *inc[] = {
        OPENAI_INC_STREAM_TEXT_DELTA,
        OPENAI_INC_STREAM_TEXT_DONE,
        OPENAI_INC_FUNC_ARGS_DELTA,
        OPENAI_INC_FUNC_ARGS_DONE,
        OPENAI_INC_REFUSAL_ANY,
        OPENAI_INC_INPUT_IMAGE_URL,
    };
    openai_v1_responses_set_includes(req, inc, sizeof inc / sizeof inc[0]);
}
