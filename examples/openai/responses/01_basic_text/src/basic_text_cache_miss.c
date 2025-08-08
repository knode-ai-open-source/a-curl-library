// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include <stdio.h>
#include <stdlib.h>

static const char *MODEL_ID="gpt-4o-mini";
static const char *PROMPT  ="Explain entropy like I'm five.";
static const char *CACHE_KEY="demo-cache-key-01";
static int round=0;

static void on_done(void*,curl_event_request_t* req,bool ok,const char*txt,
                    int,int,int){
  printf("\n[round %d] %s\n",round,ok?"OK":"FAIL");
  if(txt) puts(txt);
  if(round==1){ curl_event_loop_stop(req->loop); return; }

  /* second run – should be cache hit */
  round=1;
  curl_event_request_t *r2=openai_v1_responses_init(req->loop,
        curl_event_res_register(req->loop,strdup(getenv("OPENAI_API_KEY")),free),
        MODEL_ID);
  openai_v1_responses_sink(r2,on_done,NULL);
  openai_v1_responses_input_text(r2,PROMPT);
  openai_v1_responses_set_prompt_cache_key(r2,CACHE_KEY);
  openai_v1_responses_submit(req->loop,r2,0);
}

int main(void){
  const char *k=getenv("OPENAI_API_KEY"); if(!k||!*k){fprintf(stderr,"key?\n");return 1;}
  curl_event_loop_t *loop=curl_event_loop_init(NULL,NULL);
  curl_event_res_id kr=curl_event_res_register(loop,strdup(k),free);

  curl_event_request_t *req=openai_v1_responses_init(loop,kr,MODEL_ID);
  openai_v1_responses_sink(req,on_done,NULL);
  openai_v1_responses_input_text(req,PROMPT);
  openai_v1_responses_set_prompt_cache_key(req,CACHE_KEY);
  openai_v1_responses_submit(loop,req,0);

  curl_event_loop_run(loop);
  curl_event_loop_destroy(loop);
}
