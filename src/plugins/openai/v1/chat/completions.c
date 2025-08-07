// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/plugins/openai/chat.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <string.h>

static const char *URL = "https://api.openai.com/v1/chat/completions";

typedef struct {
    curl_event_res_id api_key_id;
    ajson_t          *messages;  /* persistent messages array */
    ajson_t          *stops;     /* persistent stop array (optional) */
} openai_v1_chat_completions_ud_t;

/* Prepare: set Authorization from API key resource. */
static bool _on_prepare(curl_event_request_t *req) {
    if (!req || !req->plugin_data) return false;
    openai_v1_chat_completions_ud_t *ud = (openai_v1_chat_completions_ud_t *)req->plugin_data;

    const char *key = (const char *)curl_event_res_peek(req->loop, ud->api_key_id);
    if (!key || !*key) {
        fprintf(stderr, "[openai.chat] missing API key\n");
        return false;
    }
    char hdr[1024];
    snprintf(hdr, sizeof(hdr), "Bearer %s", key);
    curl_event_request_set_header(req, "Authorization", hdr);
    /* Content-Type set automatically on JSON commit. */
    return true;
}

curl_event_request_t *
openai_v1_chat_completions_new(curl_event_loop_t       *loop,
                curl_event_res_id        api_key_id,
                const char              *model_id)
{
    if (!loop || api_key_id == 0 || !model_id || !*model_id) {
        fprintf(stderr, "[openai.chat] invalid args\n");
        return NULL;
    }

    curl_event_request_t *req = curl_event_request_new(0);
    if (!req) return NULL;

    curl_event_request_url(req, URL);
    curl_event_request_method(req, "POST");

    curl_event_request_set_header(req, "Accept", "application/json");
    curl_event_request_low_speed(req, 1024, 60);
    curl_event_request_enable_retries(req, 3, 2.0, 250, 20000, true);

    /* deps + plugin_data */
    curl_event_request_depend(req, api_key_id);
    openai_v1_chat_completions_ud_t *ud = (openai_v1_chat_completions_ud_t *)aml_pool_calloc(req->pool, 1, sizeof(*ud));
    ud->api_key_id = api_key_id;
    req->plugin_data = ud;
    req->plugin_data_cleanup = NULL;

    curl_event_request_on_prepare(req, _on_prepare);

    /* JSON root + model + empty messages array */
    ajson_t *root = curl_event_request_json_begin(req, /*array_root=*/false);
    ajsono_append(root, "model", ajson_encode_str(req->pool, model_id), false);

    ud->messages = ajsona(req->pool);
    ajsono_append(root, "messages", ud->messages, false);

    return req;
}

/* Messages */
void openai_v1_chat_completions_add_message(curl_event_request_t *req,
                             const char *role, const char *content)
{
    if (!req) return;
    if (!role) role = "user";
    if (!content) content = "";

    openai_v1_chat_completions_ud_t *ud = (openai_v1_chat_completions_ud_t *)req->plugin_data;
    ajson_t *root = curl_event_request_json_begin(req, false);

    if (!ud->messages) {
        ud->messages = ajsona(req->pool);
        ajsono_append(root, "messages", ud->messages, false);
    }

    ajson_t *m = ajsono(req->pool);
    ajsono_append(m, "role",    ajson_encode_str(req->pool, role),    false);
    ajsono_append(m, "content", ajson_encode_str(req->pool, content), false);
    ajsona_append(ud->messages, m);
}

/* Params */
void openai_v1_chat_completions_set_temperature(curl_event_request_t *req, float t) {
    if (!req || t < 0.0f) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "temperature", ajson_number_stringf(req->pool, "%0.2f", t), false);
}
void openai_v1_chat_completions_set_top_p(curl_event_request_t *req, float p) {
    if (!req || p <= 0.0f || p > 1.0f) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "top_p", ajson_number_stringf(req->pool, "%0.3f", p), false);
}
void openai_v1_chat_completions_set_max_tokens(curl_event_request_t *req, int n) {
    if (!req || n <= 0) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "max_tokens", ajson_number(req->pool, n), false);
}
void openai_v1_chat_completions_set_presence_penalty(curl_event_request_t *req, float v) {
    if (!req) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "presence_penalty", ajson_number_stringf(req->pool, "%0.2f", v), false);
}
void openai_v1_chat_completions_set_frequency_penalty(curl_event_request_t *req, float v) {
    if (!req) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "frequency_penalty", ajson_number_stringf(req->pool, "%0.2f", v), false);
}
void openai_v1_chat_completions_stream(curl_event_request_t *req, bool enable) {
    if (!req) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "stream", enable ? ajson_true(req->pool) : ajson_false(req->pool), false);
}
void openai_v1_chat_completions_set_user(curl_event_request_t *req, const char *user) {
    if (!req || !user) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "user", ajson_encode_str(req->pool, user), false);
}

/* Stop tokens */
void openai_v1_chat_completions_add_stop(curl_event_request_t *req, const char *token) {
    if (!req || !token) return;
    openai_v1_chat_completions_ud_t *ud = (openai_v1_chat_completions_ud_t *)req->plugin_data;
    ajson_t *root = curl_event_request_json_begin(req, false);

    if (!ud->stops) {
        ud->stops = ajsona(req->pool);
        ajsono_append(root, "stop", ud->stops, false);
    }
    ajsona_append(ud->stops, ajson_encode_str(req->pool, token));
}

/* Extra deps */
void openai_v1_chat_completions_add_dependency(curl_event_request_t *req, curl_event_res_id dep_res) {
    if (!req || !dep_res) return;
    curl_event_request_depend(req, dep_res);
}
