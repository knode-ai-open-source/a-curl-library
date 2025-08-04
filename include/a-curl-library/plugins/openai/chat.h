// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
#ifndef A_CURL_PLUGIN_OPENAI_CHAT_H
#define A_CURL_PLUGIN_OPENAI_CHAT_H

#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_output.h"
#include "a-curl-library/curl_resource.h"
#include "a-json-library/ajson.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create an unsubmitted POST /v1/chat/completions request.
 * - Adds api_key_id as a dependency (Authorization set at on_prepare).
 * - Initializes JSON root and sets {"model": model_id}.
 * - Calls curl_output_defaults(output_iface).
 */
curl_event_request_t *
openai_chat_new(curl_event_loop_t       *loop,
                curl_event_res_id        api_key_id,
                const char              *model_id,
                curl_output_interface_t *output_iface);

/* Messages */
void openai_chat_add_message(curl_event_request_t *req,
                             const char *role, const char *content);

/* Common params (set if valid) */
void openai_chat_set_temperature(curl_event_request_t *req, float t);   /* t>=0 */
void openai_chat_set_top_p(curl_event_request_t *req, float p);         /* 0<p<=1 */
void openai_chat_set_max_tokens(curl_event_request_t *req, int n);      /* n>0 */
void openai_chat_set_presence_penalty(curl_event_request_t *req, float v);
void openai_chat_set_frequency_penalty(curl_event_request_t *req, float v);
void openai_chat_stream(curl_event_request_t *req, bool enable);
void openai_chat_set_user(curl_event_request_t *req, const char *user);

/* Stop tokens: each call appends one token; uses array form */
void openai_chat_add_stop(curl_event_request_t *req, const char *token);

/* Extra dependency passthrough */
void openai_chat_add_dependency(curl_event_request_t *req,
                                curl_event_res_id dep_res);

/* Submit helper */
static inline curl_event_request_t *
openai_chat_submit(curl_event_loop_t *loop, curl_event_request_t *req, int priority) {
    return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_PLUGIN_OPENAI_CHAT_H */
