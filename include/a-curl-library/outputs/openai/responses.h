// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef CURL_OPENAI_RESPONSES_OUTPUT_H
#define CURL_OPENAI_RESPONSES_OUTPUT_H

#include "a-curl-library/curl_event_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Completion callback for /v1/responses */
typedef void (*openai_responses_complete_callback_t)(
    void                 *arg,
    curl_event_request_t *request,
    bool                  success,
    const char           *output_text,
    int                   prompt_tokens,
    int                   completion_tokens,
    int                   total_tokens);

/* Factory: returns a sink wired for curl_output_defaults() */
curl_output_interface_t *
openai_responses_output(openai_responses_complete_callback_t complete_cb,
                        void                                *complete_arg);

#ifdef __cplusplus
}
#endif
#endif /* CURL_OPENAI_RESPONSES_OUTPUT_H */
