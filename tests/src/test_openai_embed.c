// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/openai_embed.h"
#include "a-curl-library/outputs/embed.h"  // Include the embedding output API
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>

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

    // Get API key from environment variable
    char *openai_api_key = getenv("OPENAI_API_KEY");
    if (!openai_api_key) {
        fprintf(stderr, "Missing OPENAI_API_KEY environment variable.\n");
        return 1;
    }

    // Store the API key in the event loop state
    curl_event_loop_put_state(loop, "openai_api_key", openai_api_key);

    // Create the output interface using `openai_embed_output`
    curl_output_interface_t *output = openai_embed_output(
        512,                           // Expected embedding size
        embedding_complete_callback,   // Callback function for handling embeddings
        NULL                           // No additional user argument
    );

    // Define input texts for embeddings
    const char *input_texts[] = {
        "What is the meaning of life?",
        "How does quantum computing work?"
    };

    // Enqueue OpenAI embedding request
    curl_event_plugin_openai_embed_init(
        loop,
        "openai_api_key",    // Token key (stored in event loop state)
        "text-embedding-3-large",  // OpenAI model
        512,                 // Embedding dimensionality
        input_texts,         // Array of text inputs
        2,                   // Number of inputs
        output               // Using our new embedding output interface
    );

    // Run the event loop
    curl_event_loop_run(loop);

    // Cleanup
    curl_event_loop_destroy(loop);
    curl_global_cleanup();

    return 0;
}