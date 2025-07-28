// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef _CURL_OPENAI_RESPONSES_OUTPUT_H
#define _CURL_OPENAI_RESPONSES_OUTPUT_H

#include "a-curl-library/curl_event_loop.h"
#include <stdbool.h>

// Callback type for OpenAI Responses completion
typedef void (*openai_responses_complete_callback_t)(
    void *arg,
    curl_event_request_t *request,
    bool success,
    const char *output_text,
    int prompt_tokens,
    int completion_tokens,
    int total_tokens
);

// OpenAI Responses Output interface
curl_output_interface_t *openai_responses_output(
    openai_responses_complete_callback_t complete_callback,
    void *complete_callback_arg
);

#endif // _CURL_OPENAI_RESPONSES_OUTPUT_H
