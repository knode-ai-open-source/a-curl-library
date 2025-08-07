// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"

#include "a-curl-library/sinks/memory.h"             /* first call sink  */
#include "a-curl-library/sinks/openai/v1/responses.h"/* second call sink */

#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-json-library/ajson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ───────── hard-coded knobs ───────── */
static const char *MODEL_ID = "gpt-4o-mini";
static const char *PROMPT_1 = "Describe photosynthesis in two lines.";
static const char *PROMPT_2 = "Summarize the previous answer in ten words.";

/* ───────── globals needed across callbacks ───────── */
static curl_event_loop_t *g_loop       = NULL;
static curl_event_res_id g_api_key_res = 0;
static char *g_prev_response_id        = NULL;

/* ───────── second-round sink — just print text ───────── */
static void second_done(void *arg, curl_event_request_t *req,
                        bool ok, const char *text,
                        int p_tok,int c_tok,int t_tok)
{
    (void)arg;(void)p_tok;(void)c_tok;(void)t_tok;
    puts("\n── Second response ──");
    if (ok && text) puts(text); else puts("(failed)");
    curl_event_loop_stop(req->loop);
}

/* ───────── first-round sink (memory) ───────── */
static void first_done(char *data,size_t len,bool ok,
                       CURLcode,long,const char*,void*,
                       curl_event_request_t *req)
{
    if (!ok) { fprintf(stderr,"❌ first request failed\n");
               curl_event_loop_stop(req->loop); return; }

    /* parse raw JSON to fetch the response id */
    aml_pool_t *pool = aml_pool_init(1024);
    ajson_t *root = ajson_parse(pool, data, data+len);
    g_prev_response_id = ajson_to_str(ajsono_scan(root,"id"), NULL);

    printf("[first response id] %s\n", g_prev_response_id ? g_prev_response_id : "(null)");

    curl_event_request_t *req2 =
        openai_v1_responses_new(g_loop, g_api_key_res, MODEL_ID);
    openai_v1_responses_sink(req2, second_done, NULL);
    openai_v1_responses_input_text(req2, PROMPT_2);

    /* pass previous_response_id via a temp resource */
    curl_event_res_id id_res =
        curl_event_res_register(g_loop,
                                g_prev_response_id ? strdup(g_prev_response_id) : NULL,
                                free);
    openai_v1_responses_chain_previous_response(req2, id_res);

    openai_v1_responses_submit(g_loop, req2, 0);
    aml_pool_destroy(pool);
}

/* ───────── main ───────── */
int main(void)
{
    const char *key = getenv("OPENAI_API_KEY");
    if (!key || !*key) { fputs("OPENAI_API_KEY not set\n", stderr); return 1; }

    g_loop = curl_event_loop_init(NULL, NULL);
    g_api_key_res = curl_event_res_register(g_loop, strdup(key), free);

    curl_event_request_t *req1 =
        openai_v1_responses_new(g_loop, g_api_key_res, MODEL_ID);
    memory_sink(req1, first_done, NULL);

    openai_v1_responses_input_text(req1, PROMPT_1);
    openai_v1_responses_submit(g_loop, req1, 0);

    curl_event_loop_run(g_loop);
    curl_event_loop_destroy(g_loop);
    return 0;
}
