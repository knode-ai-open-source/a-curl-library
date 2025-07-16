// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/google_embed.h"
#include "a-curl-library/plugins/gcloud_token.h"
#include "a-curl-library/outputs/embed.h"  // Include the embedding output API
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>

const char *project_id = "your-google-cloud-project-id";

// **Embedding Completion Callback**
void embedding_complete_callback(
    void *arg, curl_event_request_t *request,
    bool success,
    float **embeddings, size_t num_embeddings, size_t embedding_size) {

    (void)arg; // Unused argument

    printf("Time Spent: %0.5f seconds\n", curl_event_request_time_spent(request));

    if (!success) {
        printf("Embedding request failed.\n");
        return;
    }

    // Print received embeddings
    printf("Received %zu embeddings of size %zu:\n", num_embeddings, embedding_size);
    for (size_t i = 0; i < num_embeddings; i++) {
        printf("Embedding %zu: [", i);
        for (size_t j = 0; j < embedding_size; j++) {
            printf("%0.5f%s", embeddings[i][j], (j < embedding_size - 1) ? ", " : "");
        }
        printf("]\n");
    }
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);

    // Initialize the curl event loop
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);
    curl_event_plugin_gcloud_token_init(loop, "service-account.json", "gcloud_token", false);

    // Create the output interface using `google_embed_output`
    curl_output_interface_t *output = google_embed_output(
        768,                           // Expected embedding size
        embedding_complete_callback,   // Callback function for handling embeddings
        NULL                           // No additional user argument
    );

    // Define input texts for embeddings
    char *input_texts[] = {
        "What is the meaning of life?",
        "How does quantum computing work?"
    };

    // Start Google embedding request
    curl_event_request_t *req = curl_event_plugin_google_embed_init(
        loop,
        project_id,            // Project ID
        "text-embedding-005",  // Model ID
        768,                   // Output embedding dimensionality
        "gcloud_token",        // Token key
        input_texts,
        2,                     // Number of texts
        output                 // Using our new embedding output interface
    );

    if (!req) {
        fprintf(stderr, "Failed to initialize Google embedding request.\n");
        return 1;
    }

    // Run the event loop
    curl_event_loop_run(loop);

    // Cleanup
    curl_event_loop_destroy(loop);
    // output->destroy(output);  // Destroy the output interface
    curl_global_cleanup();

    return 0;
}