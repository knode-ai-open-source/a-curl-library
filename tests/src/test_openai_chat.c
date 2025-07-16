// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/outputs/openai_chat.h"
#include "a-curl-library/plugins/openai_chat.h"
#include "a-curl-library/curl_event_loop.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>

// Example completion callback function
void chat_callback(void *arg, curl_event_request_t *req, bool success,
                   const char *assistant_response, int prompt_tokens,
                   int completion_tokens, int total_tokens) {
    (void)req;
    (void)arg;

    if (!success) {
        fprintf(stderr, "Chat request failed.\n");
        return;
    }

    printf("Assistant Response: %s\n", assistant_response);
    printf("Prompt Tokens: %d\n", prompt_tokens);
    printf("Completion Tokens: %d\n", completion_tokens);
    printf("Total Tokens: %d\n", total_tokens);
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    char *openai_api_key = getenv("OPENAI_API_KEY");
    if (!openai_api_key) {
        fprintf(stderr, "Missing OPENAI_API_KEY environment variable.\n");
        return 1;
    }

    curl_event_loop_put_state(loop, "openai_api_key", openai_api_key);

    // Set up OpenAI chat output
    curl_output_interface_t *chat_output_interface = openai_chat_output(chat_callback, NULL);

    // Define chat messages
    const char *messages[] = {
        "developer You are a helpful assistant.",
        "user Write a haiku about recursion in programming."
    };

    // Start chat request
    curl_event_plugin_openai_chat_init(
        loop,
        "openai_api_key",   // Token key
        "gpt-4o-mini",      // Model ID
        0.7,                // Temperature
        4096,               // Max tokens
        messages,
        2,
        chat_output_interface
    );

    // Run the event loop
    curl_event_loop_run(loop);

    // Cleanup
    curl_event_loop_destroy(loop);
    curl_global_cleanup();
    return 0;
}