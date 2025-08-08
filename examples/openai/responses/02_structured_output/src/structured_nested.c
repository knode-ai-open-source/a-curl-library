// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>

static void on_done(void*,curl_event_request_t*,bool ok,const char*txt,int,int,int){
    if(!ok||!txt){puts("fail");return;}
    aml_pool_t *p = aml_pool_init(1024);
    ajson_t *obj = ajson_parse_string(p, txt);
    if(!obj){ fputs("parse error\n",stderr); aml_pool_destroy(p); return; }
    puts(ajson_stringify(p, obj));  /* raw nested JSON printed */
    aml_pool_destroy(p);
}

int main(void){
    const char*key=getenv("OPENAI_API_KEY"); if(!key||!*key){puts("key?");return 1;}
    curl_event_loop_t*loop=curl_event_loop_init(NULL,NULL);
    curl_event_res_id kid=curl_event_res_register(loop,strdup(key),free);
    curl_event_request_t*r=openai_v1_responses_init(loop,kid,"gpt-4o-mini");
    openai_v1_responses_sink(r,on_done,NULL);

    const char *SCHEMA =
      "{"
      " \"type\":\"object\","
      " \"properties\":{"
      "   \"phases\":{"
      "     \"type\":\"array\","
      "     \"items\":{"
      "       \"type\":\"object\","
      "       \"properties\":{"
      "         \"name\":{\"type\":\"string\"},"
      "         \"tasks\":{"
      "           \"type\":\"array\","
      "           \"items\":{"
      "             \"type\":\"object\","
      "             \"properties\":{"
      "               \"task\":{\"type\":\"string\"},"
      "               \"owner\":{\"type\":\"string\"}"
      "             },"
      "             \"required\":[\"task\",\"owner\"],"
      "             \"additionalProperties\":false"
      "           }"
      "         }"
      "       },"
      "       \"required\":[\"name\",\"tasks\"],"
      "       \"additionalProperties\":false"
      "     }"
      "   }"
      " },"
      " \"required\":[\"phases\"],"
      " \"additionalProperties\":false"
      "}";
    openai_v1_responses_set_structured_output(r, "project_plan", SCHEMA, true);

    openai_v1_responses_input_text(r,
      "Create a JSON project plan with two phases: Planning and Execution. "
      "Each phase should have two tasks with an owner.");

    openai_v1_responses_submit(loop,r,0);
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
}
