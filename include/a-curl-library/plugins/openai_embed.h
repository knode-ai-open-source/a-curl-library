// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _OPENAI_EMBED_H
#define _OPENAI_EMBED_H

#include "a-curl-library/curl_event_loop.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

curl_event_request_t *curl_event_plugin_openai_embed_init(
    curl_event_loop_t *loop,
    const char *token_state_key,  // Bearer token from event loop state
    const char *model_id,         // OpenAI model (e.g., "text-embedding-ada-002")
    int dimensions,               // Output embedding dimensionality
    const char **input_texts,     // Array of text inputs
    size_t num_texts,             // Number of text items
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // _OPENAI_EMBED_H