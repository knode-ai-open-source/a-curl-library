// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>

static curl_event_loop_t *g_loop;
static curl_event_res_id  g_key;

/* second-round: print summaries */
static void second_done(void*,curl_event_request_t*,bool ok,const char*txt,int,int,int){
  puts(ok&&txt?txt:"(failed)");
  curl_event_loop_stop(g_loop);
}

/* first-round: parse ideas then ask for combined summaries */
static void first_done(void*,curl_event_request_t*,bool ok,const char*txt,int,int,int)
{
  if(!ok||!txt){ fputs("first failed\n",stderr); curl_event_loop_stop(g_loop); return; }
  aml_pool_t *p = aml_pool_init(1024);
  ajson_t *obj = ajson_parse_string(p, txt);
  ajson_t *arr = obj ? ajsono_scan(obj,"ideas") : NULL;
  if(!arr){ fputs("schema mismatch\n",stderr); aml_pool_destroy(p); curl_event_loop_stop(g_loop); return; }

  /* Build a prompt with the whole array */
  char *ideas_json = ajson_stringify(p, arr);

  curl_event_request_t *r2 = openai_v1_responses_init(g_loop, g_key, "gpt-4o-mini");
  openai_v1_responses_sink(r2, second_done, NULL);
  openai_v1_responses_input_text(r2,
    "For each idea in this JSON array, write a concise two-sentence summary:\n");
  openai_v1_responses_input_text(r2, ideas_json);
  openai_v1_responses_submit(g_loop, r2, 0);

  aml_pool_destroy(p);
}

int main(void){
  const char*key=getenv("OPENAI_API_KEY"); if(!key||!*key){puts("key?");return 1;}
  g_loop = curl_event_loop_init(NULL,NULL);
  g_key  = curl_event_res_register(g_loop, strdup(key), free);

  curl_event_request_t *r1 = openai_v1_responses_init(g_loop, g_key, "gpt-4o-mini");
  openai_v1_responses_sink(r1, first_done, NULL);

  const char *SCHEMA =
    "{"
    " \"type\":\"object\","
    " \"properties\":{"
    "   \"ideas\":{"
    "     \"type\":\"array\",\"minItems\":3,\"maxItems\":3,"
    "     \"items\":{\"type\":\"string\"}"
    "   }"
    " },"
    " \"required\":[\"ideas\"],"
    " \"additionalProperties\":false"
    "}";
  openai_v1_responses_set_structured_output(r1, "ideas", SCHEMA, true);

  openai_v1_responses_input_text(r1,
    "Return exactly three blog-post ideas about productivity as JSON matching the schema.");

  openai_v1_responses_submit(g_loop, r1, 0);
  curl_event_loop_run(g_loop);
  curl_event_loop_destroy(g_loop);
}
