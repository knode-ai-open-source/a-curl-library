// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
#ifndef A_CURL_PLUGIN_OPENAI_RESPONSES_H
#define A_CURL_PLUGIN_OPENAI_RESPONSES_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_output.h"
#include "a-curl-library/curl_resource.h"

/* ────────────────────────────────────────────────────────────────────────── *
 *  Helper to build a POST /v1/responses request.                            *
 *  On submit, req->output_data  → sink (openai_responses_output).           *
 *                req->plugin_data → internal per-request state.            *
 * ────────────────────────────────────────────────────────────────────────── */

#ifdef __cplusplus
extern "C" {
#endif

curl_event_request_t *
openai_responses_new(curl_event_loop_t       *loop,
                     curl_event_res_id        api_key_id,
                     const char              *model_id,
                     curl_output_interface_t *output_iface);

/* Optional chaining */
void openai_responses_chain_previous_response(curl_event_request_t *req,
                                              curl_event_res_id      prev_id_res);

/* Parameters */
void openai_responses_set_temperature      (curl_event_request_t *req, float t);
void openai_responses_set_max_output_tokens(curl_event_request_t *req, int n);
void openai_responses_set_instructions     (curl_event_request_t *req,
                                            const char *s);

/* Inputs */
void openai_responses_input_text (curl_event_request_t *req, const char *text);
void openai_responses_add_message(curl_event_request_t *req,
                                  const char *role,
                                  const char *content);
void openai_responses_set_prompt (curl_event_request_t *req,
                                  const char *id,
                                  const char *version /*nullable*/);

/* Extra deps */
void openai_responses_add_dependency(curl_event_request_t *req,
                                     curl_event_res_id     dep_res);

/* Inline submit helper */
static inline curl_event_request_t *
openai_responses_submit(curl_event_loop_t *loop,
                        curl_event_request_t *req,
                        int priority)
{
    return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_PLUGIN_OPENAI_RESPONSES_H */
