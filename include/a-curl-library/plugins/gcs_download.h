// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_event_plugin_gcs_download_H
#define _curl_event_plugin_gcs_download_H

#include "a-curl-library/curl_event_loop.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool curl_event_plugin_gcs_download_init(
    curl_event_loop_t *loop,
    const char *bucket,
    const char *object,
    const char *token_state_key,
    curl_output_interface_t *output_interface,
    long max_download_size
);

#ifdef __cplusplus
}
#endif

#endif // _plugin_gcloud_token_H