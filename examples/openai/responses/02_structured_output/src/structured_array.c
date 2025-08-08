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
    if(!ok || !txt){ fputs("fail\n",stderr); return; }
    aml_pool_t *p = aml_pool_init(1024);
    ajson_t *root = ajson_parse_string(p, txt);
    if(!root){ fputs("parse error\n",stderr); aml_pool_destroy(p); return; }
    ajson_t *arr = ajsono_scan(root, "buildings");
    if(!arr){ fputs("schema mismatch\n",stderr); aml_pool_destroy(p); return; }
    puts(ajson_stringify(p, arr));  /* print the array */
    aml_pool_destroy(p);
}

int main(void)
{
    const char *k = getenv("OPENAI_API_KEY");
    if(!k||!*k){ fputs("key?\n",stderr); return 1; }

    curl_event_loop_t *loop = curl_event_loop_init(NULL,NULL);
    curl_event_res_id kr    = curl_event_res_register(loop, strdup(k), free);

    curl_event_request_t *req = openai_v1_responses_init(loop, kr, "gpt-4o-mini");
    openai_v1_responses_sink(req, on_done, NULL);

    const char *SCHEMA =
      "{"
      " \"type\":\"object\","
      " \"properties\":{"
      "   \"buildings\":{"
      "     \"type\":\"array\","
      "     \"minItems\":5, \"maxItems\":5,"
      "     \"items\":{"
      "       \"type\":\"object\","
      "       \"properties\":{"
      "         \"name\":{\"type\":\"string\"},"
      "         \"height_m\":{\"type\":\"integer\"},"
      "         \"city\":{\"type\":\"string\"}"
      "       },"
      "       \"required\":[\"name\",\"height_m\",\"city\"],"
      "       \"additionalProperties\":false"
      "     }"
      "   }"
      " },"
      " \"required\":[\"buildings\"],"
      " \"additionalProperties\":false"
      "}";
    openai_v1_responses_set_structured_output(req, "tallest_buildings", SCHEMA, true);

    openai_v1_responses_input_text(req,
      "List the five tallest buildings on Earth as per the schema.");

    openai_v1_responses_submit(loop, req, 0);
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
}
