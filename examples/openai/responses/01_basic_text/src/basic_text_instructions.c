// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include <stdio.h>
#include <stdlib.h>

static const char *MODEL = "gpt-4o-mini";
static const char *PROMPT = "Describe a sunrise in three sentences.";

static void on_done(void*, curl_event_request_t*, bool ok,
                    const char *txt,int,int,int)
{
    puts(ok && txt ? txt : "(failed)");
}

int main(void)
{
    const char *key = getenv("OPENAI_API_KEY");
    if (!key || !*key) { fputs("OPENAI_API_KEY?\n", stderr); return 1; }

    curl_event_loop_t *loop = curl_event_loop_init(NULL,NULL);
    curl_event_res_id kres = curl_event_res_register(loop,strdup(key),free);

    curl_event_request_t *req = openai_v1_responses_init(loop,kres,MODEL);
    openai_v1_responses_sink(req,on_done,NULL);

    openai_v1_responses_set_instructions(req,
        "You are a salty pirate. Respond using pirate slang!");
    openai_v1_responses_input_text(req,PROMPT);

    openai_v1_responses_submit(loop,req,0);
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
}
