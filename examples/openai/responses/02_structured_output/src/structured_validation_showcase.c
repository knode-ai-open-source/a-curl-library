// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/memory.h"
#include <stdio.h>
#include <stdlib.h>

static void dump_or_error(char* raw, size_t n, bool ok,
                          CURLcode cc, long http, const char* err,
                          void*, curl_event_request_t*)
{
    fprintf(stderr, "HTTP=%ld CURL=%d err=%s\n", http, (int)cc, err?err:"");
    fwrite(raw, 1, n, stdout);
    fputc('\n', stdout);
}

int main(void){
  const char* key = getenv("OPENAI_API_KEY");
  if(!key||!*key){ fputs("OPENAI_API_KEY?\n", stderr); return 1; }

  curl_event_loop_t* loop = curl_event_loop_init(NULL,NULL);
  curl_event_res_id  kid  = curl_event_res_register(loop, strdup(key), free);

  /* 1) Intentionally invalid: root schema must be an object (we pass array) */
  {
    curl_event_request_t* r = openai_v1_responses_init(loop, kid, "gpt-4o-mini");
    memory_sink(r, dump_or_error, NULL);

    /* This will trigger 400 invalid_json_schema (array root not allowed) */
    const char* BAD_SCHEMA =
      "{"
      "  \"type\":\"array\","
      "  \"items\":{\"type\":\"string\"}"
      "}";

    openai_v1_responses_set_structured_output(r, "bad", BAD_SCHEMA, true);
    openai_v1_responses_input_text(r, "This should fail because root is array.");
    openai_v1_responses_submit(loop, r, 0);
  }

  /* 2) Valid request afterwards (for contrast) */
  {
    curl_event_request_t* r = openai_v1_responses_init(loop, kid, "gpt-4o-mini");
    memory_sink(r, dump_or_error, NULL);

    const char* OK_SCHEMA =
      "{"
      "  \"type\":\"object\","
      "  \"properties\":{"
      "    \"x\":{\"type\":\"integer\"}"
      "  },"
      "  \"required\":[\"x\"],"
      "  \"additionalProperties\":false"
      "}";

    openai_v1_responses_set_structured_output(r, "ok", OK_SCHEMA, true);
    openai_v1_responses_input_text(r, "Return {\"x\": 42} per schema.");
    openai_v1_responses_submit(loop, r, 0);
  }

  curl_event_loop_run(loop);
  curl_event_loop_destroy(loop);
}
