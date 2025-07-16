// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/plugins/gcloud_token.h"
#include "a-curl-library/plugins/pubsub_pull.h"
#include "a-curl-library/outputs/pubsub.h"
#include "a-curl-library/outputs/file.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *project_id = "your-google-cloud-project-id";

// Callback function to process individual Pub/Sub messages
void print_pubsub_message(pubsub_message_t *message, void *arg) {
    (void)arg; // Unused callback argument

    printf("Received Pub/Sub message:\n");
    printf("  Message ID: %s\n", message->message_id ? message->message_id : "(null)");
    printf("  Publish Time: %s\n", message->publish_time ? message->publish_time : "(null)");
    printf("  Data: %.*s\n", (int)message->length, message->data ? (char *)message->data : "(null)");

    // Print attributes
    pubsub_message_attribute_t *attr = message->attributes;
    if (attr) {
        printf("  Attributes:\n");
        while (attr) {
            printf("    %s: %s\n", attr->key ? attr->key : "(null)", attr->value ? attr->value : "(null)");
            attr = attr->next;
        }
    }

    printf("\n");
}

int main(int argc, char *argv[]) {
    // Initialize global cURL
    curl_global_init(CURL_GLOBAL_ALL);

    // Initialize the curl event loop
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    // Initialize the gcloud token plugin
    curl_event_plugin_gcloud_token_init(loop, "service-account.json", "gcloud_token", false);

    curl_event_plugin_pubsub_seek_to_timestamp_init(
        loop,
        project_id,           // Replace with your Google Cloud project ID
        "testsubscription",      // Replace with your Pub/Sub subscription ID
        "gcloud_token",              // Key to access the OAuth token
        "2025-01-20T00:00:00Z"      // Timestamp to seek to
    );

    // Configure Pub/Sub output to process messages and print them
    curl_output_interface_t *pubsub_output_interface = pubsub_output(
        loop,                        // Event loop
        project_id,           // Replace with your Google Cloud project ID
        "testsubscription",      // Replace with your Pub/Sub subscription ID
        "gcloud_token",              // Key to access the OAuth token
        print_pubsub_message,        // Callback function to process messages
        NULL,                        // Callback argument (optional, NULL in this case)
        NULL,
        NULL,
        true                         // Acknowledge messages before processing
    );

    // Initialize the Pub/Sub pull plugin to pull messages
    curl_event_plugin_pubsub_pull_init(
        loop,                        // Event loop
        project_id,           // Replace with your Google Cloud project ID
        "testsubscription",      // Replace with your Pub/Sub subscription ID
        "gcloud_token",              // Key to access the OAuth token
        10,                          // Maximum number of messages to pull in a single request
        pubsub_output_interface      // Output interface for processing messages
    );

    // Run the event loop
    curl_event_loop_run(loop);

    // Clean up
    curl_event_loop_destroy(loop);
    curl_global_cleanup();

    return 0;
}