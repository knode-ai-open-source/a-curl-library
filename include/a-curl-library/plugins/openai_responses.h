// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#ifndef _OPENAI_RESPONSES_H
#define _OPENAI_RESPONSES_H

#include "a-curl-library/curl_event_loop.h"
#include "a-memory-library/aml_pool.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Optional settings for a `/v1/responses` call.
 * Allocated and managed in an aml_pool; destroyed on init.
 */
typedef struct openai_responses_cfg {
    aml_pool_t *pool;

    float   temperature;           // default: –1 => omit
    int     max_output_tokens;     // default: 0  => omit
    char   *instructions;          // default: NULL => omit
    int     delay_ms;              // default: 0

    // input options (only one will be serialized)
    char   *input_text;
    int     num_messages;
    char  **message_roles;
    char  **message_contents;
    char   *prompt_id;
    char   *prompt_version;

    // previous_response_id from loop state (optional chain)
    char   *previous_response_id_key;

    // extra loop-state dependencies beyond API key & prev_resp
    int     num_deps;
    char  **deps;
} openai_responses_cfg_t;

/** Allocate cfg with default values */
openai_responses_cfg_t *openai_responses_cfg_new(void);

/** Set sampling temperature */
void openai_responses_cfg_temperature(openai_responses_cfg_t *c, float t);
/** Set max output tokens */
void openai_responses_cfg_max_output_tokens(openai_responses_cfg_t *c, int n);
/** Set instructions */
void openai_responses_cfg_instructions(openai_responses_cfg_t *c,
                                      const char *instr);
/** Delay before enqueue (ms) */
void openai_responses_cfg_delay(openai_responses_cfg_t *c, int ms);

/** Provide simple text input */
void openai_responses_cfg_input(openai_responses_cfg_t *c,
                                const char *text);
/** Append one message (role + content) */
void openai_responses_cfg_message(openai_responses_cfg_t *c,
                                  const char *role,
                                  const char *content);
/** Use a prompt template by id/version */
void openai_responses_cfg_prompt(openai_responses_cfg_t *c,
                                 const char *id,
                                 const char *version);

/** Chain on a previous response ID stored under this state-key */
void openai_responses_cfg_previous_response_id(openai_responses_cfg_t *c,
                                               const char *state_key);

/** Add extra dependency key (from loop state) */
void openai_responses_cfg_dependency(openai_responses_cfg_t *c,
                                     const char *state_key);

/**
 * Enqueue a `/v1/responses` call using:
 *   loop, token_state_key, model_id, cfg, output_iface
 * cfg->pool is destroyed during init, so don't reuse cfg.
 */
bool curl_event_plugin_openai_responses_init_with_cfg(
    curl_event_loop_t       *loop,
    const char              *token_state_key,
    const char              *model_id,
    openai_responses_cfg_t  *c,
    curl_output_interface_t *output_iface
);

#ifdef __cplusplus
}
#endif
#endif // _OPENAI_RESPONSES_H