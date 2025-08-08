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
                    const char *txt, int,int,int)
{
    if(!ok || !txt){ fputs("❌ request failed\n", stderr); return; }
    aml_pool_t *pool = aml_pool_init(1024);
    ajson_t *obj = ajson_parse_string(pool, txt);
    if(!obj){ fputs("parse error\n", stderr); aml_pool_destroy(pool); return; }
    puts(ajson_stringify(pool, obj));  /* pretty minimal: print object */
    aml_pool_destroy(pool);
}

int main(void)
{
    const char *key = getenv("OPENAI_API_KEY");
    if(!key||!*key){ fputs("OPENAI_API_KEY?\n", stderr); return 1; }

    curl_event_loop_t *loop = curl_event_loop_init(NULL,NULL);
    curl_event_res_id kid   = curl_event_res_register(loop, strdup(key), free);

    curl_event_request_t *req = openai_v1_responses_init(loop, kid, "gpt-4o-mini");
    openai_v1_responses_sink(req, on_done, NULL);

    aml_pool_t *p = req->pool;

    ajson_t *root = ajsb_object(p);
    ajsb_prop(p, root, "city",       ajsb_string(p));
    ajsb_prop(p, root, "tempC",      ajsb_number(p));
    ajsb_prop(p, root, "conditions", ajsb_string(p));
    const char *reqd[] = {"city","tempC","conditions"};
    ajsb_required(p, root, 3, reqd);
    ajsb_additional_properties(p, root, false);

    const char *SCHEMA =
      "{"
      " \"type\":\"object\","
      " \"properties\":{"
      "   \"city\":{\"type\":\"string\"},"
      "   \"tempC\":{\"type\":\"number\"},"
      "   \"conditions\":{\"type\":\"string\"}"
      " },"
      " \"required\":[\"city\",\"tempC\",\"conditions\"],"
      " \"additionalProperties\":false"
      "}";
    openai_v1_responses_set_structured_output(req, "weather", SCHEMA, true);

    openai_v1_responses_input_text(req,
      "Return today's weather for Paris matching the provided schema.");

    openai_v1_responses_submit(loop, req, 0);
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
}
