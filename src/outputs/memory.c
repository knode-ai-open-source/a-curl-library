// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/outputs/memory.h"
#include "a-memory-library/aml_buffer.h"
#include "a-memory-library/aml_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct memory_output_s {
    curl_output_interface_t interface;  // Base interface
    aml_buffer_t *buffer;               // Uses aml_buffer instead of char*
    memory_complete_callback_t callback; // Completion callback
    void *callback_arg;                 // Argument for the callback
} memory_output_t;

bool memory_init(curl_output_interface_t *interface, long content_length) {
    memory_output_t *mem = (memory_output_t *)interface;

    if (mem->buffer) {
        aml_buffer_destroy(mem->buffer);
        mem->buffer = NULL;
    }

    size_t initial_size = (content_length > 0) ? content_length + 1 : 1024;
    mem->buffer = aml_buffer_init(initial_size);

    return mem->buffer != NULL;
}

size_t memory_write(const void *data, size_t size, size_t nmemb, curl_output_interface_t *interface) {
    memory_output_t *mem = (memory_output_t *)interface;
    size_t total = size * nmemb;
    aml_buffer_append(mem->buffer, data, total);
    return total;
}

void memory_failure(CURLcode result, long http_code, curl_output_interface_t *interface, curl_event_request_t *req) {
    memory_output_t *mem = (memory_output_t *)interface;
    fprintf(stderr, "[memory_output] Download failed (CURLcode: %d, HTTP code: %ld).\n", result, http_code);

    const char *error_msg = curl_easy_strerror(result);
    mem->callback(aml_buffer_data(mem->buffer), aml_buffer_length(mem->buffer), false, result, http_code, error_msg,
                  mem->callback_arg, req);

    aml_buffer_destroy(mem->buffer);
    mem->buffer = NULL;
}

void memory_complete(curl_output_interface_t *interface, curl_event_request_t *req) {
    memory_output_t *mem = (memory_output_t *)interface;
    mem->callback(aml_buffer_data(mem->buffer), aml_buffer_length(mem->buffer), true, CURLE_OK, 200, NULL,
                  mem->callback_arg, req);
}

void memory_destroy(curl_output_interface_t *interface) {
    memory_output_t *mem = (memory_output_t *)interface;
    if (mem->buffer) {
        aml_buffer_destroy(mem->buffer);
        mem->buffer = NULL;
    }
    aml_free(mem);
}

curl_output_interface_t *memory_output(memory_complete_callback_t callback, void *callback_arg) {
    memory_output_t *mem = (memory_output_t *)aml_calloc(1, sizeof(memory_output_t));
    if (!mem) return NULL;

    mem->buffer = NULL;  // Ensure buffer starts as NULL
    mem->callback = callback;
    mem->callback_arg = callback_arg;

    // Assign function pointers to the interface
    mem->interface.init = memory_init;
    mem->interface.write = memory_write;
    mem->interface.failure = memory_failure;
    mem->interface.complete = memory_complete;
    mem->interface.destroy = memory_destroy;

    return (curl_output_interface_t *)mem;
}