// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/outputs/file.h"
#include "a-memory-library/aml_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct file_output_s {
    curl_output_interface_t interface; // Base interface
    FILE *file;                        // File handle
    char *filename;                    // File path
    file_complete_callback_t callback; // Completion callback
    void *callback_arg;                // Argument for the callback
} file_output_t;

bool file_init(curl_output_interface_t *interface, long content_length) {
    (void)content_length;
    file_output_t *file = (file_output_t *)interface;
    if(file->file) // for retries
        fclose(file->file);
    file->file = fopen(file->filename, "wb");
    if (!file->file) {
        fprintf(stderr, "[file_output] Failed to open file: %s\n", file->filename);
        return false;
    }
    return true;
}

size_t file_write(const void *data, size_t size, size_t nmemb, curl_output_interface_t *interface) {
    file_output_t *file = (file_output_t *)interface;
    size_t written = fwrite(data, size, nmemb, file->file);
    return written * size;
}

void file_failure(CURLcode result, long http_code, curl_output_interface_t *interface,
                  curl_event_request_t *req) {
    file_output_t *file = (file_output_t *)interface;

    // Log the failure (stderr)
    fprintf(stderr, "[file_output] Download failed (CURLcode: %d, HTTP code: %ld) for file: %s\n",
            result, http_code, file->filename);

    // Trigger the callback with failure details
    if (file->callback) {
        const char *error_msg = curl_easy_strerror(result);
        file->callback(file->filename, false, result, http_code, error_msg, file->callback_arg, req);
    }
}

void file_complete(curl_output_interface_t *interface, curl_event_request_t *req) {
    file_output_t *file = (file_output_t *)interface;

    // Log the success (stdout)
    // printf("[file_output] Download complete. File written: %s\n", file->filename);

    // Trigger the callback with success details
    if (file->callback) {
        file->callback(file->filename, true, CURLE_OK, 200, NULL, file->callback_arg, req);
    }
}

void file_destroy(curl_output_interface_t *interface) {
    file_output_t *file = (file_output_t *)interface;
    if (file->file) fclose(file->file);
    aml_free(file);
}

curl_output_interface_t *file_output(const char *filename, file_complete_callback_t callback, void *callback_arg) {
    file_output_t *file = (file_output_t *)aml_calloc(1, sizeof(file_output_t) + strlen(filename) + 1);
    if (!file) return NULL;

    file->filename = (char *)(file + 1);
    strcpy(file->filename, filename);
    file->file = NULL;
    file->callback = callback;
    file->callback_arg = callback_arg;

    // Assign function pointers to the interface
    file->interface.init = file_init;
    file->interface.write = file_write;
    file->interface.failure = file_failure;
    file->interface.complete = file_complete;
    file->interface.destroy = file_destroy;

    return (curl_output_interface_t *)file;
}