// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_pubsub_output_H
#define _curl_pubsub_output_H

#include "a-curl-library/curl_event_loop.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Represents a single attribute of a Pub/Sub message.
typedef struct pubsub_message_attribute_s {
    char *key;
    char *value;
    struct pubsub_message_attribute_s *next;
} pubsub_message_attribute_t;

// Represents a Pub/Sub message.
typedef struct {
    unsigned char *data;
    size_t length;
    char *ack_id;
    char *message_id;
    char *publish_time;
    pubsub_message_attribute_t *attributes;
    char *ordering_key;
    int delivery_attempt;
} pubsub_message_t;

// A callback type for handling individual Pub/Sub messages.
typedef void (*pubsub_message_callback_t)(
    pubsub_message_t *message,
    void *arg
);

typedef void (*pubsub_message_complete_callback_t)(
    void *arg,
    curl_event_request_t *req
);


// Initializes a Pub/Sub output interface for processing messages.
//
// Parameters:
// - loop: The curl_event_loop instance.
// - project_id: The Google Cloud project ID.
// - subscription_id: The Pub/Sub subscription ID.
// - token_state_key: The key for accessing the OAuth token in the loop state.
// - message_callback: A user-defined function for processing individual messages.
// - callback_arg: An argument passed to the message callback for user-defined context.
// - pre_ack: If true, messages are acknowledged before being passed to the message callback.
//
// Returns:
// A pointer to the initialized curl_output_interface_t object. This can be used
// with a curl_event_loop to process Pub/Sub messages.
curl_output_interface_t *pubsub_output(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    pubsub_message_callback_t message_callback,
    void *callback_arg,
    pubsub_message_complete_callback_t complete_callback,
    void *complete_callback_arg,
    bool pre_ack
);

#endif // _curl_pubsub_output_H