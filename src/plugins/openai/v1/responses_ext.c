// SPDX-License-Identifier: Apache-2.0
//
// *Extra* setters for the Responses POST builder (parameters, prompt vars,
// message parts, tools).  Builds on the core builder in responses.c.

#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-json-library/ajson.h"
#include <string.h>

#define ROOT() ajson_t *root = curl_event_request_json_begin(req, false)

/* ------------------------------------------------------------------ */
/*  tiny helpers                                                      */
static ajson_t *ensure_array(ajson_t *obj,
                             const char *k,
                             aml_pool_t *pool) {
  ajson_t *arr = ajsono_scan(obj, k);
  if (!(arr && ajson_is_array(arr))) {
    arr = ajsona(pool);
    ajsono_append(obj, k, arr, false);
  }
  return arr;
}

static ajson_t *ensure_object(ajson_t *obj,
                              const char *k,
                              aml_pool_t *pool) {
  ajson_t *o = ajsono_scan(obj, k);
  if (!(o && ajson_is_object(o))) {
    o = ajsono(pool);
    ajsono_append(obj, k, o, false);
  }
  return o;
}

/* ------------------------------------------------------------------ */
/*  basic scalar setters                                              */
#define BOOL_SET(NAME, KEY) \
void NAME(curl_event_request_t *req, bool on){ \
  if(!req) return; ROOT(); \
  ajsono_append(root, KEY, on ? ajson_true(req->pool) : ajson_false(req->pool), false); \
}

BOOL_SET(openai_v1_responses_set_background,          "background")
BOOL_SET(openai_v1_responses_set_parallel_tool_calls, "parallel_tool_calls")
BOOL_SET(openai_v1_responses_set_store,               "store")
BOOL_SET(openai_v1_responses_set_stream,              "stream")

#undef BOOL_SET

void openai_v1_responses_add_include(curl_event_request_t *req,
                                     const char *value) {
  if (!req || !value) return; ROOT();
  ajson_t *arr = ensure_array(root, "include", req->pool);
  ajsona_append(arr, ajson_encode_str(req->pool, value));
}

void openai_v1_responses_set_max_tool_calls(curl_event_request_t *req, int n){
  if (!req || n < 0) return; ROOT();
  ajsono_append(root, "max_tool_calls", ajson_number(req->pool, n), false);
}

void openai_v1_responses_set_prompt_cache_key(curl_event_request_t *req,
                                              const char *s) {
  if (!req || !s) return; ROOT();
  ajsono_append(root, "prompt_cache_key", ajson_encode_str(req->pool, s), false);
}

void openai_v1_responses_set_metadata_kv(curl_event_request_t *req,
                                         const char *k,
                                         const char *v) {
  if (!req || !k || !v) return; ROOT();
  ajson_t *meta = ensure_object(root, "metadata", req->pool);
  ajsono_append(meta, k, ajson_encode_str(req->pool, v), false);
}

void openai_v1_responses_set_reasoning_json(curl_event_request_t *req,
                                            const char *json) {
  if (!req || !json) return; ROOT();
  ajsono_append(root, "reasoning",
                ajson_parse_string(req->pool, json), false);
}

void openai_v1_responses_set_safety_identifier(curl_event_request_t *req,
                                               const char *id) {
  if (!req || !id) return; ROOT();
  ajsono_append(root, "safety_identifier",
                ajson_encode_str(req->pool, id), false);
}

void openai_v1_responses_set_service_tier(curl_event_request_t *req,
                                          const char *tier) {
  if (!req || !tier) return; ROOT();
  ajsono_append(root, "service_tier",
                ajson_encode_str(req->pool, tier), false);
}

void openai_v1_responses_set_stream_options_json(curl_event_request_t *req,
                                                 const char *json) {
  if (!req || !json) return; ROOT();
  ajsono_append(root, "stream_options",
                ajson_parse_string(req->pool, json), false);
}

void openai_v1_responses_set_top_p(curl_event_request_t *req, double p) {
  if (!req) return; ROOT();
  ajsono_append(root, "top_p",
                ajson_number_stringf(req->pool, "%g", p), false);
}

void openai_v1_responses_set_top_logprobs(curl_event_request_t *req, int n){
  if (!req || n < 0) return; ROOT();
  ajsono_append(root, "top_logprobs",
                ajson_number(req->pool, n), false);
}

void openai_v1_responses_set_truncation(curl_event_request_t *req,
                                        const char *mode) {
  if (!req || !mode) return; ROOT();
  ajsono_append(root, "truncation",
                ajson_encode_str(req->pool, mode), false);
}

/* ------------------------------------------------------------------ */
/* Prompt vars                                                        */
void openai_v1_responses_set_prompt_vars_json(curl_event_request_t *req,
                                              const char *vars_json) {
  if (!req || !vars_json) return; ROOT();
  ajson_t *prompt = ensure_object(root, "prompt", req->pool);
  ajsono_append(prompt, "variables",
                ajson_parse_string(req->pool, vars_json), false);
}

void openai_v1_responses_set_prompt_var(curl_event_request_t *req,
                                        const char *k,
                                        const char *v) {
  if (!req || !k || !v) return; ROOT();
  ajson_t *prompt  = ensure_object(root, "prompt",  req->pool);
  ajson_t *vars    = ensure_object(prompt, "variables", req->pool);
  ajsono_append(vars, k, ajson_encode_str(req->pool, v), false);
}

/* ------------------------------------------------------------------ */
/* Tools                                                              */
void openai_v1_responses_add_tool_json(curl_event_request_t *req,
                                       const char *tool_json) {
  if (!req || !tool_json) return; ROOT();
  ajson_t *arr = ensure_array(root, "tools", req->pool);
  ajsona_append(arr, ajson_parse_string(req->pool, tool_json));
}

void openai_v1_responses_set_tool_choice_json(curl_event_request_t *req,
                                              const char *choice_json) {
  if (!req || !choice_json) return; ROOT();
  ajsono_append(root, "tool_choice",
                ajson_parse_string(req->pool, choice_json), false);
}

/* ------------------------------------------------------------------ */
/* Message-style input (typed parts)                                  */
static ajson_t *input_array(curl_event_request_t *req, ajson_t *root) {
  return ensure_array(root, "input", req->pool);
}
static ajson_t *last_message(ajson_t *arr) {
  ajsona_t *n = ajsona_last(arr);
  return n ? n->value : NULL;
}

void openai_v1_responses_begin_message(curl_event_request_t *req,
                                       const char *role) {
  if (!req) return; ROOT();

  ajson_t *arr = input_array(req, root);

  ajson_t *msg = ajsono(req->pool);
  ajsono_append(msg, "role",
                ajson_encode_str(req->pool, role ? role : "user"), false);

  ajson_t *content = ajsona(req->pool);
  ajsono_append(msg, "content", content, false);
  ajsona_append(arr, msg);
}

static ajson_t *msg_content(curl_event_request_t *req, ajson_t *root) {
  ajson_t *arr = input_array(req, root);
  ajson_t *msg = last_message(arr);
  if (!msg) return NULL;
  return ajsono_scan(msg, "content");
}

void openai_v1_responses_message_add_text(curl_event_request_t *req,
                                          const char *text) {
  if (!req || !text) return; ROOT();
  ajson_t *cont = msg_content(req, root); if (!cont) return;

  ajson_t *part = ajsono(req->pool);
  ajsono_append(part, "type",
                ajson_encode_str(req->pool, "input_text"), false);
  ajsono_append(part, "text",
                ajson_encode_str(req->pool, text), false);
  ajsona_append(cont, part);
}

void openai_v1_responses_message_add_image_url(curl_event_request_t *req,
                                               const char *url,
                                               const char *mime) {
  if (!req || !url) return; ROOT();
  ajson_t *cont = msg_content(req, root); if (!cont) return;

  ajson_t *part = ajsono(req->pool);
  ajsono_append(part, "type",
                ajson_encode_str(req->pool, "input_image"), false);

  ajson_t *img = ajsono(req->pool);
  ajsono_append(img, "image_url",
                ajson_encode_str(req->pool, url), false);
  if (mime)
    ajsono_append(img, "mime_type",
                  ajson_encode_str(req->pool, mime), false);

  ajsono_append(part, "image", img, false);
  ajsona_append(cont, part);
}

void openai_v1_responses_message_add_file_id(curl_event_request_t *req,
                                             const char *file_id) {
  if (!req || !file_id) return; ROOT();
  ajson_t *cont = msg_content(req, root); if (!cont) return;

  ajson_t *part = ajsono(req->pool);
  ajsono_append(part, "type",
                ajson_encode_str(req->pool, "input_file"), false);
  ajsono_append(part, "file_id",
                ajson_encode_str(req->pool, file_id), false);
  ajsona_append(cont, part);
}

void openai_v1_responses_end_message(curl_event_request_t *) { /* no-op */ }
