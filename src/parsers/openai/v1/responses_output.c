// SPDX-License-Identifier: Apache-2.0
//
// Lightweight parser for the Responses API output JSON.  Converts raw JSON
// into a flat C struct so callers don’t need to poke through ajson.

#include "a-curl-library/parsers/openai/v1/responses_output.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_buffer.h"
#include <string.h>

/* —————————————————— helpers —————————————————— */

static ajson_t *parse_json(aml_pool_t *pool, const char *s) {
  return s ? ajson_parse_string(pool, s) : NULL;
}

/* usage{} --------------------------------------------------------------- */
bool openai_responses_parse_usage(aml_pool_t *pool,
                                  const char *raw,
                                  openai_usage_t *u) {
  if (!raw || !u) return false;
  ajson_t *root = parse_json(pool, raw);
  if (!root) return false;

  ajson_t *usage = ajsono_scan(root, "usage");
  if (!usage) return false;

  u->input_tokens  = ajson_to_int(ajsono_scan(usage, "input_tokens"),  -1);
  u->output_tokens = ajson_to_int(ajsono_scan(usage, "output_tokens"), -1);
  u->total_tokens  = ajson_to_int(ajsono_scan(usage, "total_tokens"),  -1);

  ajson_t *details = ajsono_scan(usage, "output_tokens_details");
  u->reasoning_tokens =
      details ? ajson_to_int(ajsono_scan(details, "reasoning_tokens"), -1) : -1;
  return true;
}

/* error{} / incomplete_details{} --------------------------------------- */
bool openai_responses_parse_error(aml_pool_t *pool,
                                  const char *raw,
                                  const char **err_json) {
  if (!raw || !err_json) return false;
  ajson_t *root = parse_json(pool, raw);
  if (!root) return false;
  ajson_t *err = ajsono_scan(root, "error");
  if (!err) return false;
  *err_json = ajson_stringify(pool, err);
  return true;
}

bool openai_responses_parse_incomplete(aml_pool_t *pool,
                                       const char *raw,
                                       const char **inc_json) {
  if (!raw || !inc_json) return false;
  ajson_t *root = parse_json(pool, raw);
  if (!root) return false;
  ajson_t *inc = ajsono_scan(root, "incomplete_details");
  if (!inc) return false;
  *inc_json = ajson_stringify(pool, inc);
  return true;
}

/* full output[] --------------------------------------------------------- */
bool openai_responses_parse_output(aml_pool_t *pool,
                                   const char *raw,
                                   openai_parsed_response_t *out) {
  if (!pool || !raw || !out) return false;

  memset(out, 0, sizeof(*out));           /* zero-init struct            */
  ajson_t *root = parse_json(pool, raw);   if (!root) return false;

  /* pull usage + errors so caller always gets them */
  openai_responses_parse_usage(pool, raw, &out->usage);
  openai_responses_parse_error(pool, raw, &out->error_json);
  openai_responses_parse_incomplete(pool, raw, &out->incomplete_details_json);

  ajson_t *arr = ajsono_scan(root, "output");
  if (!arr || !ajson_is_array(arr)) return true;  /* nothing to do */

  int n = ajsona_count(arr);
  out->items       = aml_pool_calloc(pool, n, sizeof(openai_output_item_t));
  out->items_count = n;

  int i = 0;
  for (ajsona_t *it = ajsona_first(arr); it; it = ajsona_next(it), ++i) {
    ajson_t *item = it->value;
    const char *type = ajson_to_str(ajsono_scan(item, "type"), "");

    out->items[i].raw_json = ajson_stringify(pool, item);

    /* Messages --------------------------------------------------------- */
    if (!strcmp(type, "message")) {
      out->items[i].kind = OPENAI_OUT_MESSAGE;
      out->items[i].role = ajson_to_str(ajsono_scan(item, "role"), NULL);

      /* aggregate all output_text parts into one convenience string */
      ajson_t *content = ajsono_scan(item, "content");
      if (content && ajson_is_array(content)) {
        aml_buffer_t *buf = aml_buffer_pool_init(pool, 256);
        for (ajsona_t *p = ajsona_first(content); p; p = ajsona_next(p)) {
          const char *part_type =
              ajson_to_str(ajsono_scan(p->value, "type"), "");
          if (!strcmp(part_type, "output_text")) {
            const char *t = ajson_to_strd(pool, ajsono_scan(p->value, "text"), "");
            if (*t) {
              if (*aml_buffer_data(buf)) aml_buffer_append(buf, "\n", 1);
              aml_buffer_append(buf, t, strlen(t));
            }
          }
        }
        out->items[i].aggregated_text = aml_buffer_data(buf);
      }
      continue;
    }

    /* Reasoning tokens -------------------------------------------------- */
    if (!strcmp(type, "reasoning")) {
      out->items[i].kind = OPENAI_OUT_REASONING;
      continue;
    }

    /* Function vs built-in tool call ----------------------------------- */
    if (!strcmp(type, "function_call")) {
      out->items[i].kind = OPENAI_OUT_FUNCTION_CALL;
    } else {
      out->items[i].kind = OPENAI_OUT_TOOL_CALL;  /* web/file/computer/code */
    }
  }

  return true;
}
