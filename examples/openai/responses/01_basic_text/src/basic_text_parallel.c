// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include <stdio.h>
#include <stdlib.h>

static const char *MODEL_ID="gpt-4o-mini";
static const char *PROMPTS[5]={
  "Give me a haiku on autumn.",
  "Define inertia in one sentence.",
  "Why is the sky blue?",
  "TL;DR of Hamlet?",
  "CPU vs GPU difference?"
};
static int done_cnt=0;
static void on_done(void*,curl_event_request_t* req,bool ok,
                    const char*txt,int,int,int){
  printf("\n--- REPLY %d ---\n",++done_cnt);
  if(ok&&txt) puts(txt); else puts("failure");
  if(done_cnt==5) curl_event_loop_stop(req->loop);
}
int main(void){
  const char *k=getenv("OPENAI_API_KEY"); if(!k||!*k){fprintf(stderr,"key?\n");return 1;}
  curl_event_loop_t *loop=curl_event_loop_init(NULL,NULL);
  curl_event_res_id kr=curl_event_res_register(loop,strdup(k),free);

  for(int i=0;i<5;i++){
    curl_event_request_t *r=openai_v1_responses_init(loop,kr,MODEL_ID);
    openai_v1_responses_sink(r, on_done,NULL);
    openai_v1_responses_input_text(r,PROMPTS[i]);
    openai_v1_responses_submit(loop,r,0);
  }
  curl_event_loop_run(loop);
  curl_event_loop_destroy(loop);
}
