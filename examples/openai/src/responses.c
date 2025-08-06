// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/outputs/openai/v1/responses.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ──────────────────────────────────────────────────────────────────────
   Simple completion-sink: print the model’s answer + token accounting
   ──────────────────────────────────────────────────────────────────── */
static void on_openai_done(
    void *arg,
    curl_event_request_t *req,
    bool success,
    const char *output_text,
    int prompt_tokens,
    int completion_tokens,
    int total_tokens)
{
    (void)arg; (void)req;
    if (!success) {
        fprintf(stderr, "[openai_chat] request failed.\n");
        return;
    }

    puts("─────────────────────────────────────────────────────────");
    puts(output_text ? output_text : "(no text)");
    printf("\n(prompt=%d, completion=%d, total=%d tokens)\n",
           prompt_tokens, completion_tokens, total_tokens);
}

/* ──────────────────────────────────────────────────────────── */

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s <MODEL_ID> \"prompt...\"\n"
        "  The OpenAI API key must be in the OPENAI_API_KEY env-var.\n", argv0);
}

int main(int argc, char **argv)
{
    if (argc < 3) { usage(argv[0]); return 1; }

    const char *model_id = argv[1];
    const char *prompt   = argv[2];

    /* Retrieve the API key */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || !api_key[0]) {
        fprintf(stderr, "OPENAI_API_KEY not set\n");
        return 1;
    }

    /* ----------  Set up curl-event loop & register the key as a resource */
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    curl_event_res_id api_key_res = curl_event_res_register(
        loop, strdup(api_key), free);             /* auto-free when released */

    /* ----------  Build the request */
    curl_output_interface_t *sink =
        openai_v1_responses_output(on_openai_done, NULL);

    curl_event_request_t *req =
        openai_v1_responses_new(loop, api_key_res, model_id, sink);

    /* fill in the request body */
    openai_v1_responses_input_text(req, prompt);
    // openai_v1_responses_set_temperature(req, 0.7f);         // optional
    // openai_v1_responses_set_max_output_tokens(req, 256);     // optional

    /* ----------  Fire! */
    openai_v1_responses_submit(loop, req, /*priority*/0);

    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
    return 0;
}
