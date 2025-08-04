// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _CURL_GOOGLE_EMBED_OUTPUT_H
#define _CURL_GOOGLE_EMBED_OUTPUT_H

#include "a-curl-library/curl_event_loop.h"
#include <stdio.h>
#include <stdbool.h>

// Callback type for embedding completion
typedef void (*gcloud_embedding_complete_callback_t)(
    void *arg, curl_event_request_t *request,
    bool success,
    float **embeddings, size_t num_embeddings, size_t embedding_size
);

curl_output_interface_t *google_embed_output(
    size_t expected_embedding_size,
    gcloud_embedding_complete_callback_t complete_callback,
    void *complete_callback_arg);

#endif // _CURL_GOOGLE_EMBED_OUTPUT_H