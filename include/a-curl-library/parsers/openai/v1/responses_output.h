// SPDX: Apache-2.0
#ifndef A_CURL_PARSERS_OPENAI_V1_RESPONSES_SINK_H
#define A_CURL_PARSERS_OPENAI_V1_RESPONSES_SINK_H

#include "a-memory-library/aml_pool.h"
#include <stdbool.h>

typedef enum {
  OPENAI_OUT_MESSAGE = 0,
  OPENAI_OUT_REASONING,
  OPENAI_OUT_TOOL_CALL,
  OPENAI_OUT_FUNCTION_CALL
} openai_output_item_kind_t;

typedef struct {
  openai_output_item_kind_t kind;
  const char *role;            /* message */
  const char *aggregated_text; /* convenience */
  const char *raw_json;        /* full item */
} openai_output_item_t;

typedef struct {
  int input_tokens, output_tokens, total_tokens, reasoning_tokens;
} openai_usage_t;

typedef struct {
  const char *error_json;              /* nullable */
  const char *incomplete_details_json; /* nullable */
  openai_usage_t usage;
  openai_output_item_t *items;
  int items_count;
} openai_parsed_response_t;

bool openai_responses_parse_output(aml_pool_t *pool, const char *raw_json,
                                   openai_parsed_response_t *out);

/* Optionals */
bool openai_responses_parse_error(aml_pool_t *pool, const char *raw_json, const char **error_json);
bool openai_responses_parse_incomplete(aml_pool_t *pool, const char *raw_json, const char **incomplete_json);
bool openai_responses_parse_usage(aml_pool_t *pool, const char *raw_json, openai_usage_t *usage);

#endif
