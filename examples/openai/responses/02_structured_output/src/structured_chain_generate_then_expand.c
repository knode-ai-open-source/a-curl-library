// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static curl_event_loop_t* g_loop; static curl_event_res_id g_key;

/* second-call sink: just print the structured {title, summary} */
static void expand_done(void*, curl_event_request_t*, bool ok,
                        const char* txt, int, int, int)
{
    puts(ok && txt ? txt : "(failed)");
}

/* first-call sink: parse { ideas: string[] } and fan out */
static void ideas_done(void*, curl_event_request_t* req, bool ok,
                       const char* txt, int, int, int)
{
    if (!ok || !txt) { fputs("first failed\n", stderr); curl_event_loop_stop(g_loop); return; }

    aml_pool_t* pool = aml_pool_init(1024);
    ajson_t* obj = ajson_parse_string(pool, txt);
    if (!obj) { fputs("parse err\n", stderr); aml_pool_destroy(pool); curl_event_loop_stop(g_loop); return; }

    ajson_t* arr = ajsono_scan(obj, "ideas");
    int n = (arr && ajson_is_array(arr)) ? ajsona_count(arr) : 0;
    if (n == 0) { fputs("no ideas\n", stderr); aml_pool_destroy(pool); curl_event_loop_stop(g_loop); return; }

    for (int i=0;i<n;++i) {
        const char* idea = ajson_to_str(ajsona_nth(arr, i), "");
        curl_event_request_t* r = openai_v1_responses_init(g_loop, g_key, "gpt-4o-mini");

        const char* SCH =
          "{"
          " \"type\":\"object\","
          " \"properties\":{"
          "   \"title\":{\"type\":\"string\"},"
          "   \"summary\":{\"type\":\"string\"}"
          " },"
          " \"required\":[\"title\",\"summary\"],"
          " \"additionalProperties\":false"
          "}";

        openai_v1_responses_set_structured_output(r, "expansion", SCH, true);
        openai_v1_responses_sink(r, expand_done, NULL);
        openai_v1_responses_input_text(r,
          "Turn this idea into a title + a two-sentence summary as per schema:");
        openai_v1_responses_input_text(r, idea);
        openai_v1_responses_submit(g_loop, r, 0);
    }

    aml_pool_destroy(pool);
}

int main(void){
  const char* key = getenv("OPENAI_API_KEY");
  if(!key||!*key){ fputs("OPENAI_API_KEY?\n", stderr); return 1; }

  g_loop = curl_event_loop_init(NULL,NULL);
  g_key  = curl_event_res_register(g_loop, strdup(key), free);

  curl_event_request_t* r = openai_v1_responses_init(g_loop, g_key, "gpt-4o-mini");

  const char* SCHEMA =
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

  openai_v1_responses_set_structured_output(r, "ideas", SCHEMA, true);
  openai_v1_responses_sink(r, ideas_done, NULL);
  openai_v1_responses_input_text(r, "Give me exactly three productivity blog ideas per schema.");

  openai_v1_responses_submit(g_loop, r, 0);
  curl_event_loop_run(g_loop);
  curl_event_loop_destroy(g_loop);
}
