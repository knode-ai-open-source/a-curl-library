// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _OPENAI_CHAT_H
#define _OPENAI_CHAT_H

#include "a-curl-library/curl_event_loop.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool curl_event_plugin_openai_chat_init(
    curl_event_loop_t *loop,
    const char *token_state_key,  // API Key from event loop state
    const char *model_id,         // OpenAI model (e.g., "gpt-4o")
    float temperature,
    int max_tokens,
    char **messages,        // Array of messages (role + content pairs)
    size_t num_messages,          // Number of messages in the conversation
    int delay_ms,
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // _OPENAI_CHAT_H