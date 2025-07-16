// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_event_plugin_gcloud_token_H
#define _curl_event_plugin_gcloud_token_H

#include "a-curl-library/curl_event_loop.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

curl_event_request_t *curl_event_plugin_gcloud_token_init(
    curl_event_loop_t *loop,
    const char *key_filename,
    const char *token_state_key,
    bool should_refresh
);

#ifdef __cplusplus
}
#endif

#endif // _plugin_gcloud_token_H