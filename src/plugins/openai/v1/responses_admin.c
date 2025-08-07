// SPDX-License-Identifier: Apache-2.0
//
// Thin wrappers around the ancillary Responses endpoints:
//
//   • GET    /v1/responses/{id}
//   • DELETE /v1/responses/{id}
//   • POST   /v1/responses/{id}/cancel
//   • GET    /v1/responses/{id}/input_items
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include <stdio.h>
#include <string.h>

/* —————————————————— internal util —————————————————— */
static void set_auth(curl_event_request_t *req, curl_event_res_id key_id) {
  const char *key = (const char *)curl_event_res_peek(req->loop, key_id);
  char hdr[1024];
  snprintf(hdr, sizeof(hdr), "Bearer %s", key ? key : "");
  curl_event_request_set_header(req, "Authorization", hdr);
  curl_event_request_set_header(req, "Accept", "application/json");
}

static curl_event_request_t *basic_req(curl_event_loop_t *loop,
                                       curl_event_res_id  key_id,
                                       const char        *url,
                                       const char        *method) {
  curl_event_request_t *req = curl_event_request_new(0);
  if (!req) return NULL;
  curl_event_request_url(req, url);
  curl_event_request_method(req, method);
  curl_event_request_depend(req, key_id);
  set_auth(req, key_id);
  curl_event_request_low_speed(req, 1024, 60);
  return req;
}

/* —————————————————— GET /responses/{id} —————————————————— */
curl_event_request_t *openai_v1_responses_get(curl_event_loop_t *loop,
                                              curl_event_res_id  key_id,
                                              const char        *id) {
  char url[1024];
  snprintf(url, sizeof(url),
           "https://api.openai.com/v1/responses/%s", id);
  return basic_req(loop, key_id, url, "GET");
}

/* —————————————————— DELETE /responses/{id} —————————————————— */
curl_event_request_t *openai_v1_responses_delete(curl_event_loop_t *loop,
                                                 curl_event_res_id  key_id,
                                                 const char        *id) {
  char url[1024];
  snprintf(url, sizeof(url),
           "https://api.openai.com/v1/responses/%s", id);
  return basic_req(loop, key_id, url, "DELETE");
}

/* —————————————————— POST /responses/{id}/cancel ———————————— */
curl_event_request_t *openai_v1_responses_cancel(curl_event_loop_t *loop,
                                                 curl_event_res_id  key_id,
                                                 const char        *id) {
  char url[1060];
  snprintf(url, sizeof(url),
           "https://api.openai.com/v1/responses/%s/cancel", id);
  return basic_req(loop, key_id, url, "POST");
}

/* —————————————————— GET /responses/{id}/input_items ———— */
curl_event_request_t *openai_v1_responses_list_input_items(
    curl_event_loop_t *loop, curl_event_res_id key_id, const char *id,
    const char *after, const char *before, int limit, const char *order,
    const char **include, int include_cnt) {

  char url[2048];
  snprintf(url, sizeof(url),
           "https://api.openai.com/v1/responses/%s/input_items", id);

  /* build query string */
  aml_buffer_t *qb = aml_buffer_init(256);
  bool first = true;
# define ADD_Q(k,v) if((v) && *(v)){ \
    aml_buffer_appendf(qb, "%c%s=%s", first?'?':'&', k, v); first=false; }

  ADD_Q("after",  after);
  ADD_Q("before", before);
  if (limit > 0) {
    char tmp[32]; snprintf(tmp, sizeof(tmp), "%d", limit);
    ADD_Q("limit", tmp);
  }
  ADD_Q("order", order);
  for (int i = 0; i < include_cnt; ++i)
    ADD_Q("include[]", include[i]);

# undef ADD_Q

  strncat(url, aml_buffer_data(qb), sizeof(url) - strlen(url) - 1);
  aml_buffer_destroy(qb);

  return basic_req(loop, key_id, url, "GET");
}
