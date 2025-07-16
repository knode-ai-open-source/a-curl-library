// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_event_plugin_cloudsql_query_H
#define _curl_event_plugin_cloudsql_query_H

#include "a-curl-library/curl_event_loop.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool curl_event_plugin_cloudsql_query_init(
    curl_event_loop_t *loop,
    const char *instance_connection_name,
    const char *database,
    const char *token_state_key,
    const char *query,
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // _curl_event_plugin_cloudsql_query_H