// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_event_plugin_spanner_session_H
#define _curl_event_plugin_spanner_session_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a Cloud Spanner session and publish its name under `session_id`.
 *
 * Dependencies:
 *   - token_id (gcloud_token resource) must be published with a
 *     gcloud_token_payload_t* (access_token).
 *
 * Publishes:
 *   - session_id -> (const char*) session name
 */
bool curl_event_plugin_spanner_session_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *instance_id,
    const char *database_id,
    curl_event_res_id  token_id,
    curl_event_res_id  session_id
);

#ifdef __cplusplus
}
#endif

#endif // _curl_event_plugin_spanner_session_H
