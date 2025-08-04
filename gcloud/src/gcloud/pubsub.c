// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/outputs/gcloud/pubsub.h"
#include "a-curl-library/plugins/gcloud/pubsub_pull.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    curl_output_interface_t interface;
    aml_pool_t *pool;
    aml_buffer_t *message_buffer;
    aml_buffer_t *ack_buffer;
    pubsub_message_callback_t message_callback;
    void *callback_arg;
    pubsub_message_complete_callback_t complete_callback;
    void *complete_callback_arg;
    bool pre_ack;
    curl_event_loop_t *loop;
    const char *project_id;
    const char *subscription_id;
    curl_event_res_id token_id;
} pubsub_output_t;

// Write callback: Append data to message buffer
static size_t pubsub_write_callback(const void *data, size_t size, size_t nmemb, curl_output_interface_t *interface) {
    pubsub_output_t *output = (pubsub_output_t *)interface;
    size_t total = size * nmemb;
    aml_buffer_append(output->message_buffer, data, total);
    return total;
}

// Helper: Parse attributes from JSON
static pubsub_message_attribute_t *parse_attributes(aml_pool_t *pool, ajson_t *attributes_obj) {
    if (!attributes_obj || !ajson_is_object(attributes_obj)) return NULL;

    pubsub_message_attribute_t *head = NULL;
    pubsub_message_attribute_t **current = &head;

    ajsono_t *attr = ajsono_first(attributes_obj);
    while (attr) {
        pubsub_message_attribute_t *attribute = aml_pool_alloc(pool, sizeof(*attribute));
        attribute->key = aml_pool_strdup(pool, attr->key);
        attribute->value = aml_pool_strdup(pool, ajson_to_strd(pool, attr->value, NULL));
        attribute->next = NULL;

        *current = attribute;
        current = &attribute->next;

        attr = ajsono_next(attr);
    }

    return head;
}

// Helper: Parse a single message
static void parse_message(ajson_t *message_obj, pubsub_output_t *output) {
    pubsub_message_t message;
    memset(&message, 0, sizeof(message));

    aml_pool_t *pool = output->pool;
    message.ack_id = ajsono_scan_strd(pool, message_obj, "ackId", NULL);
    ajson_t *data_obj = ajsono_scan(message_obj, "message");

    if (data_obj && ajson_is_object(data_obj)) {
        message.data = aml_pool_base64_decode(pool, &message.length, ajsono_scan_strd(pool, data_obj, "data", NULL));
        message.message_id = ajsono_scan_strd(pool, data_obj, "messageId", NULL);
        message.publish_time = ajsono_scan_strd(pool, data_obj, "publishTime", NULL);
        message.ordering_key = ajsono_scan_strd(pool, data_obj, "orderingKey", NULL);
        message.attributes = parse_attributes(pool, ajsono_scan(data_obj, "attributes"));
        message.delivery_attempt = ajsono_scan_int(message_obj, "deliveryAttempt", 0);
    }

    // Invoke the message callback
    if (output->message_callback) {
        output->message_callback(&message, output->callback_arg);
    }

    // Collect the ack ID for later acknowledgment
    if (message.ack_id) {
        char *ack_id = aml_pool_strdup(pool, message.ack_id);
        aml_buffer_append(output->ack_buffer, &ack_id, sizeof(ack_id));
    }
}

// Helper: Acknowledge messages using the provided ack_init plugin.
static void acknowledge_messages(pubsub_output_t *output) {
    size_t ack_count = aml_buffer_length(output->ack_buffer) / sizeof(char *);
    if (ack_count == 0) return;

    char **ack_ids = (char **)aml_buffer_data(output->ack_buffer);

    bool success = curl_event_plugin_pubsub_ack_init(
        output->loop,
        output->project_id,
        output->subscription_id,
        output->token_id,
        (const char **)ack_ids,
        ack_count
    );

    if (!success) {
        fprintf(stderr, "[acknowledge_messages] Failed to enqueue acknowledgment request.\n");
    }

    aml_buffer_clear(output->ack_buffer); // Clear buffer after acknowledgment
}

static void pubsub_on_failure(CURLcode result, long http_code, curl_output_interface_t *userdata,
                              curl_event_request_t *req) {
    pubsub_output_t *output = (pubsub_output_t *)userdata;
    if(output->complete_callback)
        output->complete_callback(output->complete_callback_arg, req);
}

// Completion callback: Parse messages and handle acknowledgment.
static void pubsub_on_complete(curl_output_interface_t *userdata, curl_event_request_t *req) {
    pubsub_output_t *output = (pubsub_output_t *)userdata;

    // fprintf( stderr, "pubsub_on_complete\n%s\n", aml_buffer_data(output->message_buffer) );
    ajson_t *json = ajson_parse_string(output->pool, aml_buffer_data(output->message_buffer));
    aml_buffer_clear(output->message_buffer); // Clear message buffer for reuse

    if (!json || ajson_is_error(json)) {
        fprintf(stderr, "[pubsub_on_complete] Failed to parse JSON.\n");
        if(output->complete_callback)
            output->complete_callback(output->complete_callback_arg, req);
        return;
    }

    ajson_t *messages = ajsono_scan(json, "receivedMessages");
    if (!messages || !ajson_is_array(messages)) {
        fprintf(stderr, "[pubsub_on_complete] No messages found.\n");
        if(output->complete_callback)
            output->complete_callback(output->complete_callback_arg, req);
        return;
    }

    if (output->pre_ack) {
        ajsona_t *node = ajsona_first(messages);
        while (node) {
            ajson_t *message_obj = node->value;
            char *ack_id = ajsono_scan_strd(output->pool, message_obj, "ackId", NULL);
            if (ack_id) {
                char *ack_id_copy = aml_pool_strdup(output->pool, ack_id);
                aml_buffer_append(output->ack_buffer, &ack_id_copy, sizeof(ack_id_copy));
            }
            node = ajsona_next(node);
        }

        acknowledge_messages(output);

        // Process messages after acknowledgment
        node = ajsona_first(messages);
        while (node) {
            parse_message(node->value, output);
            node = ajsona_next(node);
        }
    } else {
        ajsona_t *node = ajsona_first(messages);
        while (node) {
            parse_message(node->value, output);
            node = ajsona_next(node);
        }

        acknowledge_messages(output);
    }
    if(output->complete_callback)
        output->complete_callback(output->complete_callback_arg, req);
}

// Update `pubsub_init` to accept new parameters and initialize the additional fields.
static bool pubsub_init(curl_output_interface_t *interface, long content_length) {
    (void)content_length; // Unused
    pubsub_output_t *output = (pubsub_output_t *)interface;

    output->pool = aml_pool_init(4096);
    output->message_buffer = aml_buffer_init(1024);
    output->ack_buffer = aml_buffer_init(512);

    return true;
}

// Destroy the Pub/Sub output object
static void pubsub_destroy(curl_output_interface_t *interface) {
    pubsub_output_t *output = (pubsub_output_t *)interface;
    if(output->pool)
        aml_pool_destroy(output->pool); // Automatically frees all pool-allocated memory
    if(output->message_buffer)
        aml_buffer_destroy(output->message_buffer);
    if(output->ack_buffer)
        aml_buffer_destroy(output->ack_buffer);
    output->pool = NULL;
    output->message_buffer = NULL;
    output->ack_buffer = NULL;
    aml_free(output);
}


// Updated `pubsub_output` to accept loop-related arguments.
curl_output_interface_t *pubsub_output(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,           // <-- signature changed
    pubsub_message_callback_t message_callback,
    void *callback_arg,
    pubsub_message_complete_callback_t complete_callback,
    void *complete_callback_arg,
    bool pre_ack) {
    pubsub_output_t *output = aml_calloc(1, sizeof(pubsub_output_t));
    if (!output) return NULL;

    output->loop = loop;
    output->project_id = project_id;
    output->subscription_id = subscription_id;
    output->token_id = token_id;           // <-- store resource id
    output->message_callback = message_callback;
    output->callback_arg = callback_arg;
    output->pre_ack = pre_ack;
    output->complete_callback = complete_callback;
    output->complete_callback_arg = complete_callback_arg;
    output->interface.init = pubsub_init;
    output->interface.write = pubsub_write_callback;
    output->interface.failure = NULL;
    output->interface.complete = pubsub_on_complete;
    output->interface.destroy = pubsub_destroy;

    return (curl_output_interface_t *)output;
}