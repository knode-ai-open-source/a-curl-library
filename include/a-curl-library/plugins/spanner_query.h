// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_event_plugin_spanner_query_H
#define _curl_event_plugin_spanner_query_H

/*
    Unfortunately, the spanner_query plugin is not working as expected.  Google doesn't seem to provide
    a rest API for querying spanner databases.
*/

#include "a-curl-library/curl_event_loop.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool curl_event_plugin_spanner_query_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *instance_id,
    const char *database_id,
    const char *token_state_key,
    const char *session_state_key,
    const char *sql_statement,
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // _curl_event_plugin_spanner_query_H
