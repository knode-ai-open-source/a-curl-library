// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>

static void on_done(void*, curl_event_request_t*, bool ok,
                    const char* txt, int, int, int)
{
    if (!ok || !txt) { fputs("fail\n", stderr); return; }
    aml_pool_t* pool = aml_pool_init(1024);
    ajson_t* obj = ajson_parse_string(pool, txt);
    if (!obj) { fputs("parse\n", stderr); aml_pool_destroy(pool); return; }
    puts(ajson_stringify(pool, obj));
    aml_pool_destroy(pool);
}

int main(void){
  const char* key = getenv("OPENAI_API_KEY");
  if(!key||!*key){ fputs("OPENAI_API_KEY?\n", stderr); return 1; }

  curl_event_loop_t* loop = curl_event_loop_init(NULL,NULL);
  curl_event_res_id  kid  = curl_event_res_register(loop, strdup(key), free);
  curl_event_request_t* r = openai_v1_responses_init(loop, kid, "gpt-4o-mini");

  openai_v1_responses_sink(r, on_done, NULL);

  const char* SCHEMA =
   "{"
   " \"type\":\"object\","
   " \"properties\":{"
   "  \"term\":{\"type\":\"string\"},"
   "  \"filters\":{"
   "    \"anyOf\":["
   "      {\"type\":\"object\",\"properties\":{\"price_min\":{\"type\":\"number\"},\"price_max\":{\"type\":\"number\"}},"
   "       \"required\":[\"price_min\",\"price_max\"],\"additionalProperties\":false},"
   "      {\"type\":\"object\",\"properties\":{\"categories\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},"
   "       \"required\":[\"categories\"],\"additionalProperties\":false}"
   "    ]"
   "  }"
   " },"
   " \"required\":[\"term\",\"filters\"],"
   " \"additionalProperties\":false"
   "}";

  openai_v1_responses_set_structured_output(r,"search_query",SCHEMA,true);
  openai_v1_responses_input_text(r,
    "Build a normalized search query from: "
    "‘running shoes under $120; categories: trail, waterproof’.");

  openai_v1_responses_submit(loop, r, 0);
  curl_event_loop_run(loop);
  curl_event_loop_destroy(loop);
}
