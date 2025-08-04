// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_event_plugin_spanner_query_H
#define _curl_event_plugin_spanner_query_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute a Spanner SQL statement using an existing session.
 *
 * Dependencies:
 *   - token_id   (gcloud_token payload)
 *   - session_id (published string from spanner_session)
 */
bool curl_event_plugin_spanner_query_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *instance_id,
    const char *database_id,
    curl_event_res_id  token_id,
    curl_event_res_id  session_id,
    const char *sql_statement,
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // _curl_event_plugin_spanner_query_H
