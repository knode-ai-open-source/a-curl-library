// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _CURL_OPENAI_V1_EMBEDDINGS_SINK_H
#define _CURL_OPENAI_V1_EMBEDDINGS_SINK_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include <stdbool.h>
#include <stddef.h>

/* Callback signature ------------------------------------------------------ */
typedef void (*openai_v1_embeddings_complete_callback_t)(
    void *arg,
    curl_event_request_t *request,
    bool   success,
    float **embeddings,           /* NULL on failure */
    size_t  num_embeddings,
    size_t  embedding_size        /* 0 if unknown or failure */
);

/* Factory ----------------------------------------------------------------- */
curl_sink_interface_t *openai_v1_embeddings_sink(
    curl_event_request_t *req,
    size_t expected_embedding_size,                  /* 0 = accept any */
    openai_v1_embeddings_complete_callback_t complete_cb,
    void *complete_cb_arg);

#endif /* _CURL_OPENAI_V1_EMBEDDINGS_SINK_H */
