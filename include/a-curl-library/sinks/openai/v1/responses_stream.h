// SPDX: Apache-2.0
#ifndef A_CURL_SINK_OPENAI_V1_RESPONSES_STREAM_H
#define A_CURL_SINK_OPENAI_V1_RESPONSES_STREAM_H

#include "a-curl-library/curl_event_request.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void (*on_text_delta)(void *arg, const char *utf8, size_t n);
  void (*on_message_done)(void *arg);
  void (*on_tool_call)(void *arg, const char *tool_json_delta);
  void (*on_usage)(void *arg, int input_tok, int sink_tok, int total_tok, int reasoning_tok);
  void (*on_event)(void *arg, const char *event, const char *raw_json);
  void (*on_error)(void *arg, int http, const char *err_json);
  void (*on_completed)(void *arg);
} openai_v1_responses_stream_callbacks_t;

curl_sink_interface_t *
openai_v1_responses_stream_sink_new(
    curl_event_request_t *req,
    const openai_v1_responses_stream_callbacks_t *cbs,
    void *arg);

#ifdef __cplusplus
}
#endif
#endif
