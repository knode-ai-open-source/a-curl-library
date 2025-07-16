// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_memory_output_H
#define _curl_memory_output_H

#include "a-curl-library/curl_event_loop.h"

#include <stdio.h>
#include <stdbool.h>

// Define a callback type for completion
typedef void (*memory_complete_callback_t)(
    char *data,
    size_t length,
    bool success,
    CURLcode result,      // CURLcode (0 if success)
    long http_code,       // HTTP status code (0 if success)
    const char *error_msg, // Error message (NULL if success)
    void *arg,            // User-defined argument
    curl_event_request_t *req
);

curl_output_interface_t *memory_output(memory_complete_callback_t callback,
                                       void *callback_arg);

#endif