// SPDX-License-Identifier: Apache-2.0
/* Parallel follow-up demo – Structured Outputs */

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/memory.h"
#include "a-curl-library/sinks/openai/v1/responses.h"
#include "a-curl-library/parsers/openai/v1/responses_output.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DBG(fmt, ...) fprintf(stderr, "DEBUG " fmt "\n", ##__VA_ARGS__)
#define SHOW_LIMIT 1024

static curl_event_loop_t *g_loop;
static curl_event_res_id  g_api_key;

/* — fan-out bookkeeping — */
typedef struct {
    int    total, done;
    char **summaries;
} fan_ctx_t;

/* per-summary ctx (lives in rq->pool) */
typedef struct { fan_ctx_t *fan; int idx; } follow_ctx_t;

/* ------------------------------------------------------------------ */
static void summary_done(void *arg, curl_event_request_t* /*req*/,
                         bool ok,const char *txt,int,int,int)
{
    follow_ctx_t *f = arg;
    fan_ctx_t *fan  = f->fan;
    int idx = f->idx;

    DBG("summary[%d] %s len=%zu", idx, ok?"OK":"FAIL", txt?strlen(txt):0);
    fan->summaries[idx] = ok && txt ? strdup(txt) : strdup("(failed)");

    if (++fan->done == fan->total) {
      puts("\n── Final assembled summaries ──");
      for (int i = 0; i < fan->total; ++i) {
        printf("[%d] %s\n", i + 1, fan->summaries[i]);
        free(fan->summaries[i]);
      }
      free(fan->summaries);
      free(fan);
      curl_event_loop_stop(g_loop);
    }
}

/* ------------------------------------------------------------------ */
static void ideas_done(char *raw,size_t len,bool ok,
                       CURLcode cc,long http,const char *err,
                       void*, curl_event_request_t *req)
{
    DBG("ideas call ok=%d HTTP=%ld CURL=%d err=\"%s\"",ok,http,cc,err?err:"");
    DBG("raw (%zu) %.1024s%s",len,raw?raw:"",len>SHOW_LIMIT?"…":"");
    if(!ok){ curl_event_loop_stop(g_loop); return; }

    aml_pool_t *pool = req->pool;                     /* ← use existing pool */
    openai_parsed_response_t out={0};
    if(!openai_responses_parse_output(pool,raw,&out)||out.items_count==0){
        fputs("parse failure\n",stderr); curl_event_loop_stop(g_loop); return;
    }
    ajson_t *obj = ajson_parse_string(pool,out.items[0].aggregated_text);
    ajson_t *arr = obj ? ajsono_scan(obj,"ideas") : NULL;
    if(!arr||!ajson_is_array(arr)||ajsona_count(arr)!=3){
        fputs("schema mismatch\n",stderr); curl_event_loop_stop(g_loop); return;
    }

    int n = ajsona_count(arr);
    fan_ctx_t *fan = calloc(1,sizeof *fan);
    fan->total = n; fan->summaries = calloc(n,sizeof(char*));

    for(int i=0;i<n;++i){
        const char *idea = ajson_to_str(ajsona_nth(arr,i),"");
        DBG("idea[%d] \"%s\"",i,idea);

        curl_event_request_t *rq =
            openai_v1_responses_init(g_loop,g_api_key,"gpt-4o-mini");
        follow_ctx_t *fctx = aml_pool_zalloc(rq->pool,sizeof *fctx);
        fctx->fan = fan; fctx->idx = i;

        openai_v1_responses_sink(rq,summary_done,fctx);
        openai_v1_responses_input_text(rq,
            "Write a two-sentence summary of this blog-post idea:\n");
        openai_v1_responses_input_text(rq,idea);
        openai_v1_responses_submit(g_loop,rq,0);
    }
}

/* ------------------------------------------------------------------ */
int main(void)
{
    const char *key = getenv("OPENAI_API_KEY");
    if(!key||!*key){ fputs("OPENAI_API_KEY?\n",stderr); return 1; }

    g_loop = curl_event_loop_init(NULL,NULL);
    g_api_key = curl_event_res_register(g_loop,strdup(key),free);

    curl_event_request_t *req =
        openai_v1_responses_init(g_loop,g_api_key,"gpt-4o-mini");
    memory_sink(req,ideas_done,NULL);

    const char *SCHEMA =
        "{"
        "\"type\":\"object\","
        "\"properties\":{"
        "  \"ideas\":{"
        "    \"type\":\"array\","
        "    \"minItems\":3,\"maxItems\":3,"
        "    \"items\":{\"type\":\"string\"}"
        "  }"
        "},"
        "\"required\":[\"ideas\"],"
        "\"additionalProperties\":false"
        "}";

    openai_v1_responses_set_structured_output(req,"ideas",SCHEMA,true);

    openai_v1_responses_input_text(
        req,
        "Return exactly three blog-post ideas about productivity as JSON "
        "matching the given schema.");

    DBG("request → %.1024s%s",
        ajson_stringify(req->pool,curl_event_request_json_root(req)),
        strlen(ajson_stringify(req->pool,curl_event_request_json_root(req)))>
            SHOW_LIMIT?"…":"");

    openai_v1_responses_submit(g_loop,req,0);
    curl_event_loop_run(g_loop);
    curl_event_loop_destroy(g_loop);
    return 0;
}
