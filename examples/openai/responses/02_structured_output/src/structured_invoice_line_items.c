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
    if (!obj) { fputs("parse error\n", stderr); aml_pool_destroy(pool); return; }

    /* compute subtotal */
    ajson_t* items = ajsono_scan(obj, "line_items");
    double subtotal = 0.0;
    if (items && ajson_is_array(items)) {
      int n = ajsona_count(items);
      for (int i=0;i<n;++i){
        ajson_t* it = ajsona_nth(items,i);
        int qty = ajsono_scan_int(it,"qty",0);
        double price = ajsono_scan_double(it,"unit_price",0.0);
        subtotal += qty * price;
      }
    }
    puts(ajson_stringify(pool, obj));
    fprintf(stderr, "DEBUG subtotal=%.2f\n", subtotal);
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
    "  \"vendor\":{\"type\":\"string\"},"
    "  \"currency\":{\"type\":\"string\",\"enum\":[\"USD\",\"EUR\",\"GBP\",\"JPY\"]},"
    "  \"line_items\":{\"type\":\"array\",\"minItems\":1,"
    "    \"items\":{"
    "      \"type\":\"object\","
    "      \"properties\":{"
    "        \"desc\":{\"type\":\"string\"},"
    "        \"qty\":{\"type\":\"integer\",\"minimum\":1},"
    "        \"unit_price\":{\"type\":\"number\",\"minimum\":0}"
    "      },"
    "      \"required\":[\"desc\",\"qty\",\"unit_price\"],"
    "      \"additionalProperties\":false"
    "    }}"
    " },"
    " \"required\":[\"vendor\",\"currency\",\"line_items\"],"
    " \"additionalProperties\":false"
    "}";

  openai_v1_responses_set_structured_output(r,"invoice",SCHEMA,true);
  openai_v1_responses_input_text(r,
    "Parse this invoice summary into structured fields: "
    "‘Acme billed us for 2× Pro Seats at $49.99, and 3× Storage add-ons at $5.’ "
    "Assume currency USD.");

  openai_v1_responses_submit(loop, r, 0);
  curl_event_loop_run(loop);
  curl_event_loop_destroy(loop);
}
