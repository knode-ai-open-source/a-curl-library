// SPDX: Apache-2.0
#ifndef A_CURL_PLUGIN_OPENAI_V1_RESPONSES_CANCEL_H
#define A_CURL_PLUGIN_OPENAI_V1_RESPONSES_CANCEL_H
#include "a-curl-library/curl_event_loop.h"
#ifdef __cplusplus
extern "C" {
#endif
curl_event_request_t *openai_v1_responses_cancel(curl_event_loop_t *loop,
    curl_event_res_id api_key_id, const char *response_id,
    curl_sink_interface_t *sink);
#ifdef __cplusplus
}
#endif
#endif
