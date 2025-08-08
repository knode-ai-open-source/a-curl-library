// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/memory.h"
#include "a-json-library/ajson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_done(char* raw, size_t n, bool ok,
                    CURLcode, long, const char*, void*, curl_event_request_t* req)
{
    if (!ok) { fputs("request failed\n", stderr); return; }

    aml_pool_t* pool = req->pool; /* reuse */
    ajson_t* root = ajson_parse(pool, raw, raw + n);
    if (!root) { fputs("bad json\n", stderr); return; }

    /* Walk output[0].content[*] to see if there's a refusal part */
    ajson_t* out = ajsono_scan(root, "output");
    if (out && ajson_is_array(out) && ajsona_count(out) > 0) {
        ajson_t* msg  = ajsona_first(out)->value;
        ajson_t* cont = ajsono_scan(msg, "content");
        if (cont && ajson_is_array(cont)) {
            int m = ajsona_count(cont);
            for (int i=0;i<m;++i) {
                ajson_t* part = ajsona_nth(cont, i);
                const char* type = ajsono_scan_str(part, "type", "");
                if (type && strcmp(type, "refusal") == 0) {
                    const char* why = ajsono_scan_str(part, "refusal", "");
                    fprintf(stderr, "Refusal: %s\n", why ? why : "(none)");
                    puts("{\"is_allowed\":false,\"categories\":[],\"reason\":\"refused\"}");
                    return;
                }
            }
            /* Otherwise, take first output_text.text */
            for (int i=0;i<m;++i) {
                ajson_t* part = ajsona_nth(cont, i);
                const char* type = ajsono_scan_str(part, "type", "");
                if (type && strcmp(type, "output_text") == 0) {
                    const char* txt = ajsono_scan_str(part, "text", "{}"); /* decoded by *_str */
                    aml_pool_t* tmp = aml_pool_init(512);
                    ajson_t* obj = ajson_parse_string(tmp, txt);
                    puts(ajson_stringify(tmp, obj));
                    aml_pool_destroy(tmp);
                    return;
                }
            }
        }
    }
    fputs("unexpected envelope\n", stderr);
}

int main(void){
  const char* key = getenv("OPENAI_API_KEY");
  if(!key||!*key){ fputs("OPENAI_API_KEY?\n", stderr); return 1; }

  curl_event_loop_t* loop = curl_event_loop_init(NULL,NULL);
  curl_event_res_id  kid  = curl_event_res_register(loop, strdup(key), free);
  curl_event_request_t* r = openai_v1_responses_init(loop, kid, "gpt-4o-mini");

  memory_sink(r, on_done, NULL);

  const char *SCHEMA =
    "{"
    " \"type\":\"object\","
    " \"properties\":{"
    "   \"is_allowed\":{\"type\":\"boolean\"},"
    "   \"categories\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
    "   \"reason\":{\"type\":\"string\"}"
    " },"
    " \"required\":[\"is_allowed\",\"categories\",\"reason\"],"
    " \"additionalProperties\":false"
    "}";

  openai_v1_responses_set_structured_output(r,"moderation",SCHEMA,true);
  openai_v1_responses_input_text(r,
    "Moderate this text and report: ‘<user content here>’. "
    "If unsafe, refuse per safety policy.");

  openai_v1_responses_submit(loop, r, 0);
  curl_event_loop_run(loop);
  curl_event_loop_destroy(loop);
}
