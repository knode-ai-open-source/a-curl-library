// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_event_plugin_pubsub_pull_H
#define _curl_event_plugin_pubsub_pull_H

#include "a-curl-library/curl_event_loop.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the Pub/Sub pull plugin.
 *
 * @param loop The curl_event_loop instance.
 * @param project_id The Google Cloud project ID.
 * @param subscription_id The Pub/Sub subscription ID.
 * @param token_state_key The key for accessing the OAuth token in the loop state.
 * @param max_messages The maximum number of messages to pull in a single request.
 * @param output_interface The output interface for handling the pulled messages.
 *
 * @return true if the request was enqueued successfully, false otherwise.
 */
bool curl_event_plugin_pubsub_pull_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    int max_messages,
    curl_output_interface_t *output_interface
);

/**
 * Initialize the Pub/Sub acknowledgment plugin.
 *
 * @param loop The curl_event_loop instance.
 * @param project_id The Google Cloud project ID.
 * @param subscription_id The Pub/Sub subscription ID.
 * @param token_state_key The key for accessing the OAuth token in the loop state.
 * @param ack_ids An array of ack IDs to acknowledge.
 * @param num_ack_ids The number of ack IDs in the array.
 *
 * @return true if the request was enqueued successfully, false otherwise.
 */
bool curl_event_plugin_pubsub_ack_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    const char **ack_ids,
    size_t num_ack_ids
);

bool curl_event_plugin_pubsub_seek_to_timestamp_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    const char *timestamp
);

bool curl_event_plugin_pubsub_seek_to_snapshot_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    const char *snapshot
);

#ifdef __cplusplus
}
#endif

#endif // _curl_event_plugin_pubsub_pull_H
