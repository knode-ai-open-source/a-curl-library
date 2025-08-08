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
    aml_pool_t* pool = aml_pool_init(2048);
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
   "  \"type\":{\"type\":\"string\",\"enum\":[\"div\",\"button\",\"header\",\"section\",\"field\",\"form\"]},"
   "  \"label\":{\"type\":\"string\"},"
   "  \"children\":{\"type\":\"array\",\"items\":{\"$ref\":\"#\"}},"
   "  \"attributes\":{\"type\":\"array\",\"items\":{"
   "     \"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"}},"
   "     \"required\":[\"name\",\"value\"],\"additionalProperties\":false}}"
   " },"
   " \"required\":[\"type\",\"label\",\"children\",\"attributes\"],"
   " \"additionalProperties\":false"
   "}";

  openai_v1_responses_set_structured_output(r,"ui",SCHEMA,true);
  openai_v1_responses_input_text(r,"Generate a small UI tree: a header with a button inside.");

  openai_v1_responses_submit(loop, r, 0);
  curl_event_loop_run(loop);
  curl_event_loop_destroy(loop);
}
