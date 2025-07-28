// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/plugins/openai_responses.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_alloc.h"
#include "a-curl-library/curl_event_loop.h"
#include <stdio.h>
#include <string.h>

static const char *URL = "https://api.openai.com/v1/responses";

static bool _on_prepare(curl_event_request_t *req) {
    char *key = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!key) {
        fprintf(stderr, "[responses] missing API key\n");
        return false;
    }
    char hdr[1024];
    snprintf(hdr, sizeof(hdr), "Bearer %s", key);
    curl_event_loop_update_header(req, "Authorization", hdr);
    curl_event_loop_update_header(req, "Content-Type",  "application/json");
    aml_free(key);
    return true;
}

openai_responses_cfg_t *openai_responses_cfg_new(void) {
    openai_responses_cfg_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->pool                    = aml_pool_init(16*1024);
    c->temperature             = -1.0f;
    c->max_output_tokens       = 0;
    c->instructions            = NULL;
    c->delay_ms                = 0;
    c->input_text              = NULL;
    c->num_messages            = 0;
    c->message_roles           = NULL;
    c->message_contents        = NULL;
    c->prompt_id               = NULL;
    c->prompt_version          = NULL;
    c->previous_response_id_key = NULL;
    c->num_deps                = 0;
    c->deps                    = NULL;
    return c;
}

void openai_responses_cfg_temperature(openai_responses_cfg_t *c, float t) {
    c->temperature = t;
}
void openai_responses_cfg_max_output_tokens(openai_responses_cfg_t *c, int n) {
    c->max_output_tokens = n;
}
void openai_responses_cfg_instructions(openai_responses_cfg_t *c, const char *instr) {
    if (instr) c->instructions = aml_pool_strdup(c->pool, instr);
}
void openai_responses_cfg_delay(openai_responses_cfg_t *c, int ms) {
    c->delay_ms = ms;
}
void openai_responses_cfg_input(openai_responses_cfg_t *c, const char *text) {
    if (text) c->input_text = aml_pool_strdup(c->pool, text);
}
void openai_responses_cfg_message(openai_responses_cfg_t *c,
                                  const char *role,
                                  const char *content) {
    // grow roles
    char **nr = aml_pool_alloc(c->pool,
        sizeof(char*) * (c->num_messages + 1));
    if (c->message_roles)
        memcpy(nr, c->message_roles,
            sizeof(char*) * c->num_messages);
    nr[c->num_messages] = aml_pool_strdup(c->pool, role);
    c->message_roles    = nr;
    // grow contents
    char **nc = aml_pool_alloc(c->pool,
        sizeof(char*) * (c->num_messages + 1));
    if (c->message_contents)
        memcpy(nc, c->message_contents,
            sizeof(char*) * c->num_messages);
    nc[c->num_messages] = aml_pool_strdup(c->pool, content);
    c->message_contents = nc;
    c->num_messages++;
}
void openai_responses_cfg_prompt(openai_responses_cfg_t *c,
                                 const char *id,
                                 const char *version) {
    if (id)      c->prompt_id      = aml_pool_strdup(c->pool, id);
    if (version) c->prompt_version = aml_pool_strdup(c->pool, version);
}
void openai_responses_cfg_previous_response_id(openai_responses_cfg_t *c,
                                               const char *key) {
    if (key) c->previous_response_id_key = aml_pool_strdup(c->pool, key);
}
void openai_responses_cfg_dependency(openai_responses_cfg_t *c,
                                     const char *state_key) {
    char **nd = aml_pool_alloc(c->pool,
        sizeof(char*) * (c->num_deps + 1));
    if (c->deps)
        memcpy(nd, c->deps,
            sizeof(char*) * c->num_deps);
    nd[c->num_deps] = aml_pool_strdup(c->pool, state_key);
    c->deps       = nd;
    c->num_deps++;
}

bool curl_event_plugin_openai_responses_init_with_cfg(
    curl_event_loop_t       *loop,
    const char              *token_state_key,
    const char              *model_id,
    openai_responses_cfg_t  *c,
    curl_output_interface_t *iface
) {
    if (!loop||!token_state_key||!model_id||!c||!iface) {
        fprintf(stderr, "[responses_init] invalid args\n");
        return false;
    }
    ajson_t *root = ajsono(c->pool);
    ajsono_append(root, "model",
                  ajson_encode_str(c->pool, model_id),
                  false);
    if (c->temperature >= 0.0f)
        ajsono_append(root, "temperature",
            ajson_number_stringf(c->pool, "%0.2f", c->temperature),
            false);
    if (c->max_output_tokens > 0)
        ajsono_append(root, "max_output_tokens",
            ajson_number(c->pool, c->max_output_tokens),
            false);
    if (c->instructions)
        ajsono_append(root, "instructions",
            ajson_encode_str(c->pool, c->instructions),
            false);

    // previous_response_id
    if (c->previous_response_id_key) {
        char *prev = curl_event_loop_get_state(loop,
                          c->previous_response_id_key);
        if (prev) {
            ajsono_append(root, "previous_response_id",
                ajson_encode_str(c->pool, prev),
                false);
            aml_free(prev);
        }
    }

    // inputs: prompt > messages > text
    if (c->prompt_id) {
        ajson_t *p = ajsono(c->pool);
        ajsono_append(p, "id",
            ajson_encode_str(c->pool, c->prompt_id),
            false);
        if (c->prompt_version)
            ajsono_append(p, "version",
                ajson_encode_str(c->pool, c->prompt_version),
                false);
        ajsono_append(root, "prompt", p, false);
    } else if (c->num_messages) {
        ajson_t *arr = ajsona(c->pool);
        for (int i=0; i<c->num_messages; i++) {
            ajson_t *m = ajsono(c->pool);
            ajsono_append(m, "role",
                ajson_encode_str(c->pool, c->message_roles[i]),
                false);
            ajsono_append(m, "content",
                ajson_encode_str(c->pool, c->message_contents[i]),
                false);
            ajsona_append(arr, m);
        }
        ajsono_append(root, "input", arr, false);
    } else if (c->input_text) {
        ajsono_append(root, "input",
            ajson_encode_str(c->pool, c->input_text),
            false);
    } else {
        fprintf(stderr, "[responses_init] no input provided\n");
        aml_pool_destroy(c->pool);
        return false;
    }

    // build dependencies: API key + prev_resp_key + extras
    int total = 1 + (c->previous_response_id_key ? 1 : 0) + c->num_deps;
    char **deps = aml_pool_alloc(c->pool,
        sizeof(char*) * (total + 1));
    int idx = 0;
    deps[idx++] = (char*)token_state_key;
    if (c->previous_response_id_key)
        deps[idx++] = c->previous_response_id_key;
    for (int i=0; i<c->num_deps; i++) deps[idx++] = c->deps[i];
    deps[idx] = NULL;

    curl_event_request_t req = {0};
    req.loop         = loop;
    req.url          = (char*)URL;
    req.method       = "POST";
    req.dependencies = deps;
    req.post_data    = ajson_stringify(c->pool, root);
    req.on_prepare   = _on_prepare;
    curl_output_defaults(&req, iface);
    req.low_speed_limit = 1024;
    req.low_speed_time  = 60;
    req.max_retries     = 3;

    if (!curl_event_loop_enqueue(loop, &req, -c->delay_ms)) {
        fprintf(stderr, "[responses_init] enqueue failed\n");
        aml_pool_destroy(c->pool);
        return false;
    }

    aml_pool_destroy(c->pool);
    return true;
}