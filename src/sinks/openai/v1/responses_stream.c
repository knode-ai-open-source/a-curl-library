// SPDX-License-Identifier: Apache-2.0
//
// Simple Server-Sent-Events sink for `stream:true` Responses runs.
// Buffers one event at a time and dispatches to client callbacks.

#include "a-curl-library/sinks/openai/v1/responses_stream.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_buffer.h"
#include <string.h>

typedef struct {
  curl_sink_interface_t iface;

  openai_v1_responses_stream_callbacks_t cb;
  void *arg;

  aml_buffer_t *line;   /* accum current line             */
  aml_buffer_t *data;   /* accum current event “data:”    */
  char          event[96];
  int           http;   /* HTTP status on failure         */
} sse_sink_t;

/* —————————————————— helpers —————————————————— */
static void dispatch(sse_sink_t *s) {
  const char *payload = aml_buffer_data(s->data);

  if (!*s->event && !*payload) return;           /* nothing built yet */

  /* Fast paths for common events */
  if (!strcmp(s->event, "response.output_text.delta") && s->cb.on_text_delta) {
    s->cb.on_text_delta(s->arg, payload, strlen(payload));

  } else if (!strcmp(s->event, "response.message.completed") &&
             s->cb.on_message_done) {
    s->cb.on_message_done(s->arg);

  } else if ((!strcmp(s->event, "response.tool_call.delta") ||
              !strcmp(s->event, "response.function_call.delta")) &&
             s->cb.on_tool_call) {
    s->cb.on_tool_call(s->arg, payload);

  } else if (!strcmp(s->event, "response.usage") && s->cb.on_usage) {
    aml_pool_t *pool = s->iface.pool;
    ajson_t *j = ajson_parse_string(pool, payload);
    int in  = j ? ajson_to_int(ajsono_scan(j, "input_tokens"),  -1) : -1;
    int out = j ? ajson_to_int(ajsono_scan(j, "output_tokens"), -1) : -1;
    int tot = j ? ajson_to_int(ajsono_scan(j, "total_tokens"),  -1) : -1;
    int rea = j ? ajson_to_int(ajsono_scan(j, "reasoning_tokens"), -1) : -1;
    s->cb.on_usage(s->arg, in, out, tot, rea);

  } else if (!strcmp(s->event, "response.error") && s->cb.on_error) {
    s->cb.on_error(s->arg, s->http, payload);

  } else if (!strcmp(s->event, "response.completed") &&
             s->cb.on_completed) {
    s->cb.on_completed(s->arg);

  } else if (s->cb.on_event) {
    s->cb.on_event(s->arg, s->event, payload);
  }

  aml_buffer_clear(s->data);
  s->event[0] = '\0';
}

/* —————————————————— curl_sink_interface —————————————————— */
static bool init_sink(curl_sink_interface_t *iface, long) {
  sse_sink_t *s = (void *)iface;
  s->line = aml_buffer_init(256);
  s->data = aml_buffer_init(1024);
  return s->line && s->data;
}

static size_t write_cb(const void *ptr, size_t size, size_t nmemb,
                       curl_sink_interface_t *iface) {
  sse_sink_t *s = (void *)iface;
  const char *p = (const char *)ptr;
  size_t      n = size * nmemb;

  for (size_t i = 0; i < n; ++i) {
    char c = p[i];

    if (c == '\n') {                   /* line-terminated ------------- */
      const char *line = aml_buffer_data(s->line);

      if (*line == '\0') {             /* blank → event done           */
        dispatch(s);
      } else if (!strncmp(line, "event:", 6)) {
        const char *e = line + 6;  while (*e == ' ') ++e;
        strncpy(s->event, e, sizeof(s->event) - 1);
        s->event[sizeof(s->event) - 1] = '\0';

      } else if (!strncmp(line, "data:", 5)) {
        const char *d = line + 5;  while (*d == ' ') ++d;
        if (*aml_buffer_data(s->data))
          aml_buffer_append(s->data, "\n", 1);   /* multi-line data */
        aml_buffer_append(s->data, d, strlen(d));
      }

      aml_buffer_clear(s->line);

    } else if (c != '\r') {
      aml_buffer_append(s->line, &c, 1);
    }
  }
  return n;
}

static void complete_cb(curl_sink_interface_t *iface,
                        curl_event_request_t    *) {
  dispatch((sse_sink_t *)iface);   /* flush trailing event (if any) */
}

static void failure_cb(CURLcode, long http,
                       curl_sink_interface_t *iface,
                       curl_event_request_t    *) {
  sse_sink_t *s = (void *)iface;
  s->http = (int)http;
  if (s->cb.on_error) s->cb.on_error(s->arg, s->http, NULL);
}

static void destroy_cb(curl_sink_interface_t *iface) {
  sse_sink_t *s = (void *)iface;
  if (s->line) aml_buffer_destroy(s->line);
  if (s->data) aml_buffer_destroy(s->data);
}

/* —————————————————— factory —————————————————— */
curl_sink_interface_t *
openai_v1_responses_stream_sink_init(
    curl_event_request_t *req,
    const openai_v1_responses_stream_callbacks_t *cb,
    void *arg) {

  sse_sink_t *s = aml_pool_zalloc(req->pool, sizeof(*s));
  if (!s) return NULL;

  if (cb) memcpy(&s->cb, cb, sizeof(*cb));
  s->arg = arg;

  s->iface.pool     = req->pool;
  s->iface.init     = init_sink;
  s->iface.write    = write_cb;
  s->iface.complete = complete_cb;
  s->iface.failure  = failure_cb;
  s->iface.destroy  = destroy_cb;

  curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);

  return (curl_sink_interface_t *)s;
}
