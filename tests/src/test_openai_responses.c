// SPDX-FileCopyrightText: 2025 YourName
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Your Name <you@example.com>

#include "a-curl-library/outputs/openai_responses.h"
#include "a-curl-library/plugins/openai_responses.h"
#include "a-curl-library/curl_event_loop.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>

void responses_callback(void *arg,
                        curl_event_request_t *req,
                        bool success,
                        const char *output_text,
                        int prompt_tokens,
                        int completion_tokens,
                        int total_tokens)
{
    (void)req; (void)arg;
    if (!success) {
        fprintf(stderr, "Responses request failed.\n");
        return;
    }
    printf("Output Text: %s\n", output_text);
    printf("Prompt Tokens: %d\n", prompt_tokens);
    printf("Completion Tokens: %d\n", completion_tokens);
    printf("Total Tokens: %d\n", total_tokens);
}

int main(void) {
    curl_global_init(CURL_GLOBAL_ALL);
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    char *key = getenv("OPENAI_API_KEY");
    if (!key) {
        fprintf(stderr, "Missing OPENAI_API_KEY env var.\n");
        return 1;
    }
    curl_event_loop_put_state(loop, "openai_api_key", key);

    openai_responses_cfg_t *cfg = openai_responses_cfg_new();
    openai_responses_cfg_temperature(cfg, 0.5f);
    openai_responses_cfg_max_output_tokens(cfg, 256);
    openai_responses_cfg_delay(cfg, 0);
    openai_responses_cfg_message(cfg, "developer", "You are a helpful assistant.");
    openai_responses_cfg_message(cfg, "user",      "Write a haiku about recursion.");

    curl_output_interface_t *iface =
        openai_responses_output(responses_callback, NULL);

    curl_event_plugin_openai_responses_init_with_cfg(
        loop,
        "openai_api_key",
        "gpt-4.1",
        cfg,
        iface
    );

    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
    curl_global_cleanup();
    return 0;
}
