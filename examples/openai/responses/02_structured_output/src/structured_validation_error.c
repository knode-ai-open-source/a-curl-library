// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/memory.h"
#include "a-curl-library/parsers/openai/v1/responses_output.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>

/* Use memory_sink so we can read server error JSON on HTTP 400 */
static void on_done(char *raw, size_t n, bool ok,
                    CURLcode cc, long http, const char *err,
                    void*, curl_event_request_t*)
{
    aml_pool_t *p = aml_pool_init(1024);
    if(!ok || http>=400){
        const char *err_json = NULL;
        if(openai_responses_parse_error(p, raw, &err_json) && err_json)
            fprintf(stderr,"Schema error → %s\n", err_json);
        else
            fprintf(stderr,"HTTP %ld (CURL %d). Body: %.*s\n",
                    http,(int)cc,(int)n,(raw?raw:""));
    }else{
        puts("(unexpected success)");  /* we *wanted* an error */
    }
    aml_pool_destroy(p);
}

int main(void){
    const char*key=getenv("OPENAI_API_KEY"); if(!key||!*key){puts("key?");return 1;}
    curl_event_loop_t*loop=curl_event_loop_init(NULL,NULL);
    curl_event_res_id kid=curl_event_res_register(loop,strdup(key),free);

    curl_event_request_t*r=openai_v1_responses_init(loop,kid,"gpt-4o-mini");
    memory_sink(r,on_done,NULL);

    /* strict schema that prompt cannot satisfy (age <=10 for Einstein) */
    const char *SCHEMA =
      "{"
      " \"type\":\"object\","
      " \"properties\":{"
      "   \"name\":{\"type\":\"string\"},"
      "   \"age\":{\"type\":\"integer\",\"maximum\":10}"
      " },"
      " \"required\":[\"name\",\"age\"],"
      " \"additionalProperties\":false"
      "}";
    openai_v1_responses_set_structured_output(r, "person", SCHEMA, true);

    openai_v1_responses_input_text(r,
      "Describe Albert Einstein with name and age.");

    openai_v1_responses_submit(loop,r,0);
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
}
