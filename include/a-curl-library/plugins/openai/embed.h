// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0
#ifndef A_CURL_PLUGIN_OPENAI_EMBED_H
#define A_CURL_PLUGIN_OPENAI_EMBED_H

#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_output.h"
#include "a-curl-library/curl_resource.h"
#include "a-json-library/ajson.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create an unsubmitted POST /v1/embeddings request.
 * - Adds api_key_id as a dependency (Authorization set at on_prepare).
 * - Initializes JSON root and sets {"model": model_id}.
 * - Calls curl_output_defaults(output_iface).
 */
curl_event_request_t *
openai_embed_new(curl_event_loop_t       *loop,
                 curl_event_res_id        api_key_id,
                 const char              *model_id,
                 curl_output_interface_t *output_iface);

/* Inputs */
void openai_embed_add_text(curl_event_request_t *req, const char *text);
/* Convenience: add many at once */
void openai_embed_add_texts(curl_event_request_t *req,
                            const char **texts, size_t n);

/* Params */
void openai_embed_set_dimensions(curl_event_request_t *req, int dimensions);       /* n>0 */
void openai_embed_set_encoding_format(curl_event_request_t *req, const char *fmt); /* "float" or "base64" */
void openai_embed_set_user(curl_event_request_t *req, const char *user);

/* Extra deps */
void openai_embed_add_dependency(curl_event_request_t *req, curl_event_res_id dep_res);

/* Submit helper */
static inline curl_event_request_t *
openai_embed_submit(curl_event_loop_t *loop, curl_event_request_t *req, int priority) {
    return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_PLUGIN_OPENAI_EMBED_H */
