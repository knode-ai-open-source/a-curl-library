// SPDX-License-Identifier: Apache-2.0
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/openai/v1/responses.h"
#include "a-curl-library/sinks/openai/v1/responses_stream.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define DBG(fmt, ...) fprintf(stderr, "DEBUG " fmt "\n", ##__VA_ARGS__)

typedef struct {
  aml_pool_t   *pool;
  aml_buffer_t *buf;      // accumulates the JSON text (from deltas)
  size_t        env_count;
  size_t        delta_bytes;
  bool          printed;
} stream_ctx_t;

static void emit_accumulated(stream_ctx_t *ctx, const char *why) {
  const char *acc = aml_buffer_data(ctx->buf);
  size_t      al  = aml_buffer_length(ctx->buf);
  DBG("emit (%s) len=%zu", why ? why : "?", al);

  if (!acc || al == 0) { DBG("nothing to emit"); return; }

  ajson_t *obj = ajson_parse(ctx->pool, (char*)acc, (char*)acc + al);
  if (obj && !ajson_is_error(obj)) {
    puts(ajson_stringify(ctx->pool, obj));   // pretty JSON
  } else {
    // Raw fallback if parsing fails
    fwrite(acc, 1, al, stdout);
    fputc('\n', stdout);
  }
  fflush(stdout);
  ctx->printed = true;
}

/* Handle a single JSON envelope (already isolated) */
static void handle_envelope(stream_ctx_t *ctx, const char *json, size_t n) {
  if (!json || !n) return;

  ajson_t *ev = ajson_parse(ctx->pool, (char*)json, (char*)json + n);
  if (!ev || ajson_is_error(ev)) {
    DBG("envelope[%zu] parse error", ctx->env_count);
    return;
  }

  const char *type = ajsono_scan_str(ev, "type", "");
  if (!type || !*type) {
    DBG("envelope[%zu] has no type", ctx->env_count);
    return;
  }

  DBG("envelope[%zu] type=%s", ctx->env_count, type);

  if (strcmp(type, "response.output_text.delta") == 0) {
    char *delta_dec = ajsono_scan_strd(ctx->pool, ev, "delta", "");
    size_t m = delta_dec ? strlen(delta_dec) : 0;
    ctx->delta_bytes += m;
    DBG("  delta bytes=%zu (total=%zu)", m, ctx->delta_bytes);
    if (m) aml_buffer_append(ctx->buf, delta_dec, m);
  }
  else if (strcmp(type, "response.output_text.done") == 0) {
    // Some transports send this as an envelope; go ahead and emit.
    emit_accumulated(ctx, "done-envelope");
  }
}

/* Split one callback chunk into multiple JSON envelopes by brace depth. */
static void on_text_delta(void *arg, const char *utf8, size_t n) {
  stream_ctx_t *ctx = (stream_ctx_t *)arg;
  if (!utf8 || n == 0) return;

  DBG("chunk bytes=%zu preview=%.120s%s", n, utf8, n>120?"…":"");

  size_t start = 0;
  int depth = 0;
  bool in_str = false;
  bool esc = false;

  for (size_t i = 0; i < n; ++i) {
    char c = utf8[i];

    if (in_str) {
      if (esc) { esc = false; }
      else if (c == '\\') { esc = true; }
      else if (c == '"') { in_str = false; }
      continue;
    }

    if (c == '"') { in_str = true; continue; }
    if (c == '{') { if (depth++ == 0) start = i; }
    else if (c == '}') {
      if (--depth == 0) {
        // one full JSON object [start..i]
        ctx->env_count++;
        handle_envelope(ctx, utf8 + start, i - start + 1);
      }
    }
  }
}

static void on_message_done(void *arg) { DBG("message_done"); (void)arg; }
static void on_completed(void *arg)    { DBG("completed");    (void)arg; }

static void on_event(void *arg, const char *ev, const char *raw) {
  stream_ctx_t *ctx = (stream_ctx_t *)arg;
  DBG("on_event type=%s raw_len=%zu", ev ? ev : "(null)", raw ? strlen(raw) : 0);

  // In your trace, "response.output_text.done" arrives here (not as a delta).
  if (ev && strcmp(ev, "response.output_text.done") == 0) {
    emit_accumulated(ctx, "done-event");
  }
}

int main(void) {
  const char *key = getenv("OPENAI_API_KEY");
  if (!key || !*key) { fputs("OPENAI_API_KEY?\n", stderr); return 1; }

  curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);
  curl_event_res_id  kid  = curl_event_res_register(loop, strdup(key), free);
  curl_event_request_t *r = openai_v1_responses_init(loop, kid, "gpt-4o-mini");

  /* Structured Outputs schema */
  const char *SCHEMA =
    "{"
      "\"type\":\"object\","
      "\"properties\":{"
        "\"attributes\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
        "\"colors\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
        "\"animals\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}"
      "},"
      "\"required\":[\"attributes\",\"colors\",\"animals\"],"
      "\"additionalProperties\":false"
    "}";

  openai_v1_responses_set_structured_output(r, "entities", SCHEMA, true);
  openai_v1_responses_set_stream(r, true);

  // Build stream ctx in the request pool
  stream_ctx_t *ctx = (stream_ctx_t *)aml_pool_zalloc(r->pool, sizeof(*ctx));
  ctx->pool = r->pool;
  ctx->buf  = aml_buffer_pool_init(r->pool, 512);

  openai_v1_responses_stream_callbacks_t cbs = {
    .on_text_delta   = on_text_delta,
    .on_message_done = on_message_done,
    .on_completed    = on_completed,
    .on_event        = on_event
  };
  openai_v1_responses_stream_sink_init(r, &cbs, ctx);

  openai_v1_responses_input_text(
    r,
    "Extract entities from: ‘The red fox jumps over a lazy dog near the blue river.’"
  );

  DBG("submit request");
  openai_v1_responses_submit(loop, r, 0);
  curl_event_loop_run(loop);
  DBG("loop exited");

  // Safety: if the backend never sent output_text.done for some reason
  if (!ctx->printed && aml_buffer_length(ctx->buf) > 0) {
    emit_accumulated(ctx, "loop-exit-fallback");
  }

  curl_event_loop_destroy(loop);
  return 0;
}
