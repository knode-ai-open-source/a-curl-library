// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include <stdio.h>
#include <stdlib.h>

static const char *MODEL_ID = "gpt-4o-mini";
static const char *PROMPT   = "Haiku about autumn leaves";

static void on_done(void*,curl_event_request_t*,bool ok,
                    const char *txt,int p,int c,int t)
{
    puts(ok? "\n✅ success\n":"\n❌ failed\n");
    if(txt) puts(txt);
    printf("(prompt=%d completion=%d total=%d)\n",p,c,t);
}
int main(void){
    const char *k=getenv("OPENAI_API_KEY"); if(!k||!*k){fprintf(stderr,"key?\n");return 1;}
    curl_event_loop_t *loop=curl_event_loop_init(NULL,NULL);
    curl_event_res_id kr=curl_event_res_register(loop,strdup(k),free);

    curl_event_request_t *req=openai_v1_responses_init(loop,kr,MODEL_ID);
    openai_v1_responses_sink(req,on_done,NULL);

    openai_v1_responses_input_text(req,PROMPT);
    openai_v1_responses_set_top_logprobs(req,5);
    openai_v1_responses_add_include(req,"message.output_text.logprobs");

    openai_v1_responses_submit(loop,req,0);
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
}
