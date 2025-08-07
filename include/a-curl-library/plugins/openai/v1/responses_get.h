// SPDX: Apache-2.0
#ifndef A_CURL_PLUGIN_OPENAI_V1_RESPONSES_GET_H
#define A_CURL_PLUGIN_OPENAI_V1_RESPONSES_GET_H
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#ifdef __cplusplus
extern "C" {
#endif
curl_event_request_t *openai_v1_responses_get(curl_event_loop_t *loop,
    curl_event_res_id api_key_id, const char *response_id,
    curl_sink_interface_t *sink /* JSON sink or SSE sink if stream=true */);
#ifdef __cplusplus
}
#endif
#endif
