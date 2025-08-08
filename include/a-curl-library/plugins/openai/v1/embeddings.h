// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
#ifndef A_CURL_PLUGIN_OPENAI_V1_EMBEDDINGS_H
#define A_CURL_PLUGIN_OPENAI_V1_EMBEDDINGS_H

#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ────────────────────────────────────────────────────────────────────────── */
/*  Builder                                                                  */
/* ────────────────────────────────────────────────────────────────────────── */
/* Creates an un-submitted POST /v1/embeddings request and wires:            *
 *   – api_key_id  → dependency (Bearer token added in on_prepare)          *
 *   – model_id    → {"model": "..."}                                       *
 * Per-request working state is stored in req->plugin_data.                 */
curl_event_request_t *
openai_v1_embeddings_init(curl_event_loop_t       *loop,
                 curl_event_res_id        api_key_id,
                 const char              *model_id);

/* Helpers – inputs -------------------------------------------------------- */
void openai_v1_embeddings_add_text (curl_event_request_t *req, const char *text);
void openai_v1_embeddings_add_texts(curl_event_request_t *req,
                            const char **texts, size_t n);

/* Helpers – parameters ---------------------------------------------------- */
void openai_v1_embeddings_set_dimensions      (curl_event_request_t *req, int dimensions);
void openai_v1_embeddings_set_encoding_format (curl_event_request_t *req, const char *fmt);
void openai_v1_embeddings_set_user            (curl_event_request_t *req, const char *user);

/* Extra dependency passthrough ------------------------------------------- */
void openai_v1_embeddings_add_dependency(curl_event_request_t *req,
                                 curl_event_res_id     dep_res);

/* Convenience submit helper ---------------------------------------------- */
static inline curl_event_request_t *
openai_v1_embeddings_submit(curl_event_loop_t *loop,
                    curl_event_request_t *req,
                    int priority)
{
    return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_PLUGIN_OPENAI_V1_EMBEDDINGS_H */
