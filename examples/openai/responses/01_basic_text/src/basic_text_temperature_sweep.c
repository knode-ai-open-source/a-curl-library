// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include <stdio.h>
#include <stdlib.h>

static const char *MODEL = "gpt-4o-mini";
static const char *PROMPT = "Write one sentence praising teamwork.";
static const float TEMPS[] = {0.0f, 0.4f, 0.7f, 1.0f};
static int done_cnt = 0;

static void on_done(void*, curl_event_request_t *req, bool ok,
                    const char *txt, int p,int c,int t)
{
    printf("\n[T=%.1f] %s\n", TEMPS[done_cnt], ok && txt ? txt : "(failed)");
    if (++done_cnt == (int)(sizeof TEMPS / sizeof *TEMPS))
        curl_event_loop_stop(req->loop);
}

int main(void)
{
    const char *key = getenv("OPENAI_API_KEY");
    if (!key || !*key) { fputs("OPENAI_API_KEY?\n", stderr); return 1; }

    curl_event_loop_t *loop = curl_event_loop_init(NULL,NULL);
    curl_event_res_id kres = curl_event_res_register(loop,strdup(key),free);

    for (size_t i = 0; i < sizeof TEMPS / sizeof *TEMPS; ++i) {
        curl_event_request_t *r = openai_v1_responses_init(loop,kres,MODEL);
        openai_v1_responses_sink(r,on_done,NULL);
        openai_v1_responses_set_temperature(r,TEMPS[i]);
        openai_v1_responses_input_text(r,PROMPT);
        openai_v1_responses_submit(loop,r,0);
    }
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
}
