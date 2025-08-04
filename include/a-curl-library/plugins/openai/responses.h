// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
#ifndef A_CURL_PLUGIN_OPENAI_RESPONSES_H
#define A_CURL_PLUGIN_OPENAI_RESPONSES_H

#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_output.h"
#include "a-curl-library/curl_resource.h"
#include "a-json-library/ajson.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create an unsubmitted POST /v1/responses request.
 * - Adds api_key_id as a dependency (Authorization set at on_prepare).
 * - Initializes JSON root and sets {"model": model_id}.
 * - Calls curl_output_defaults(output_iface).
 */
curl_event_request_t *
openai_responses_new(curl_event_loop_t       *loop,
                     curl_event_res_id        api_key_id,
                     const char              *model_id,
                     curl_output_interface_t *output_iface);

/* Optional: chain on a previous response id (string resource). */
void openai_responses_chain_previous_response(curl_event_request_t *req,
                                              curl_event_res_id prev_id_res);

/* Top-level params */
void openai_responses_set_temperature(curl_event_request_t *req, float t);    /* t>=0 => include */
void openai_responses_set_max_output_tokens(curl_event_request_t *req, int n);/* n>0  => include */
void openai_responses_set_instructions(curl_event_request_t *req, const char *s);

/* Inputs (choose one style) */
void openai_responses_input_text(curl_event_request_t *req, const char *text);
void openai_responses_add_message(curl_event_request_t *req,
                                  const char *role, const char *content);
void openai_responses_set_prompt(curl_event_request_t *req,
                                 const char *id, const char *version /*nullable*/);

/* Extra dependency passthrough */
void openai_responses_add_dependency(curl_event_request_t *req,
                                     curl_event_res_id dep_res);

/* Submit helper */
static inline curl_event_request_t *
openai_responses_submit(curl_event_loop_t *loop, curl_event_request_t *req, int priority) {
    return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_PLUGIN_OPENAI_RESPONSES_H */
