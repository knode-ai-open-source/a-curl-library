// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
#ifndef A_CURL_PLUGIN_OPENAI_V1_RESPONSES_H
#define A_CURL_PLUGIN_OPENAI_V1_RESPONSES_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-json-library/ajson.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Named include constants (handy + typo-proof) ---------- */
#define OPENAI_INC_STREAM_TEXT_DELTA   "response.output_text.delta"
#define OPENAI_INC_STREAM_TEXT_DONE    "response.output_text.done"
#define OPENAI_INC_FUNC_ARGS_DELTA     "response.function_call.arguments.delta"
#define OPENAI_INC_FUNC_ARGS_DONE      "response.function_call.arguments.done"
#define OPENAI_INC_INPUT_IMAGE_URL     "message.input_image.image_url"
#define OPENAI_INC_REASONING_BLOB      "reasoning.encrypted_content"
#define OPENAI_INC_REFUSAL_ANY         "response.refusal"

/* POST /v1/responses (base) */
curl_event_request_t *
openai_v1_responses_init(curl_event_loop_t *loop,
                         curl_event_res_id  api_key_id,
                         const char        *model_id);

void openai_v1_responses_set_structured_output(
    curl_event_request_t *req,
    const char *name,        /* e.g. "ideas" */
    const char *schema_json,
    bool        strict);     /* true ⇒ add "strict":true */

void openai_v1_responses_set_structured_output_json(
    curl_event_request_t *req,
    const char *name,        /* e.g. "ideas" */
    ajson_t *json,
    bool        strict);     /* true ⇒ add "strict":true */

/* Chaining */
void openai_v1_responses_chain_previous_response(curl_event_request_t *req,
                                                 curl_event_res_id     prev_id_res);

/* Basic params */
void openai_v1_responses_set_temperature(curl_event_request_t *req, float t);
void openai_v1_responses_set_max_output_tokens(curl_event_request_t *req, int n);
void openai_v1_responses_set_instructions(curl_event_request_t *req, const char *s);

/* Inputs */
void openai_v1_responses_input_text(curl_event_request_t *req, const char *text);
void openai_v1_responses_add_message(curl_event_request_t *req, const char *role, const char *content);

/* ---------- Full parameter surface ---------- */
void openai_v1_responses_set_background          (curl_event_request_t*, bool on);
void openai_v1_responses_add_include             (curl_event_request_t*, const char *value); /* low-level escape hatch */
void openai_v1_responses_set_parallel_tool_calls (curl_event_request_t*, bool on);
void openai_v1_responses_set_max_tool_calls      (curl_event_request_t*, int n);
void openai_v1_responses_set_prompt_cache_key    (curl_event_request_t*, const char *s);
void openai_v1_responses_set_metadata_kv         (curl_event_request_t*, const char *k, const char *v);
void openai_v1_responses_set_reasoning_json      (curl_event_request_t*, const char *json);
void openai_v1_responses_set_safety_identifier   (curl_event_request_t*, const char *id);
void openai_v1_responses_set_service_tier        (curl_event_request_t*, const char *tier); /* auto|default|flex|priority */
void openai_v1_responses_set_store               (curl_event_request_t*, bool on);
void openai_v1_responses_set_stream              (curl_event_request_t*, bool on);
void openai_v1_responses_set_stream_options_json (curl_event_request_t*, const char *json);
void openai_v1_responses_set_top_p               (curl_event_request_t*, double p);
void openai_v1_responses_set_top_logprobs        (curl_event_request_t*, int n);
void openai_v1_responses_set_truncation          (curl_event_request_t*, const char *mode);  /* auto|disabled */

/* Prompt templates */
void openai_v1_responses_set_prompt              (curl_event_request_t*, const char *id, const char *version /*nullable*/);
void openai_v1_responses_set_prompt_vars_json    (curl_event_request_t*, const char *vars_json);
void openai_v1_responses_set_prompt_var          (curl_event_request_t*, const char *k, const char *v);

/* Message builder w/ typed parts */
void openai_v1_responses_begin_message           (curl_event_request_t*, const char *role); /* user|system|assistant */
void openai_v1_responses_message_add_text        (curl_event_request_t*, const char *text);
void openai_v1_responses_message_add_image_url   (curl_event_request_t*, const char *url, const char *mime /*nullable*/);
void openai_v1_responses_message_add_file_id     (curl_event_request_t*, const char *file_id);
void openai_v1_responses_end_message             (curl_event_request_t*);

/* Tools */
void openai_v1_responses_add_tool_json           (curl_event_request_t*, const char *tool_json);
void openai_v1_responses_set_tool_choice_json    (curl_event_request_t*, const char *choice_json);

/* Extra deps */
void openai_v1_responses_add_dependency          (curl_event_request_t *req, curl_event_res_id dep_res);

/* ---------- NEW: include helpers & presets ---------- */
void openai_v1_responses_clear_includes          (curl_event_request_t *req);
void openai_v1_responses_set_includes            (curl_event_request_t *req,
                                                  const char *const *items, size_t count);

/* Presets (convenience) */
void openai_v1_responses_include_stream_text_minimal    (curl_event_request_t *req);
void openai_v1_responses_include_stream_text_and_tools  (curl_event_request_t *req);
void openai_v1_responses_include_input_image_urls       (curl_event_request_t *req);
void openai_v1_responses_include_reasoning_encrypted    (curl_event_request_t *req);
void openai_v1_responses_include_refusal                (curl_event_request_t *req);
void openai_v1_responses_include_debug                  (curl_event_request_t *req);

/* Inline submit helper */
static inline curl_event_request_t *
openai_v1_responses_submit(curl_event_loop_t *loop, curl_event_request_t *req, int priority) {
  return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_PLUGIN_OPENAI_V1_RESPONSES_H */
