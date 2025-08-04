// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_event_plugin_google_embed_H
#define _curl_event_plugin_google_embed_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_output.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void curl_event_plugin_google_embed_set_rate(void);

curl_event_request_t *curl_event_plugin_google_embed_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *model_id,
    int output_dimensionality,
    curl_event_res_id  token_id,            /* gcloud_token resource id */
    char **input_text,                      /* array of NUL-terminated strings */
    size_t num_texts,
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // _curl_event_plugin_google_embed_H
