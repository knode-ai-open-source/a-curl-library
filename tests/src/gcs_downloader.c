// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/plugins/gcloud_token.h"
#include "a-curl-library/plugins/gcs_download.h"
#include "a-curl-library/outputs/file.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// --------------------------- CONFIG ----------------------------------

#define MAX_ACTIVE_REQUESTS 500
#define MAX_RETRIES 3

#define GOOGLE_PROJECT_ID "your-google-cloud-project-id"

// --------------------------- CALLBACKS -------------------------------
FILE *log_file;

// Logging callback for download results
void log_download_result(
    const char *filename,
    bool success,
    CURLcode result,
    long http_code,
    const char *error_msg,
    void *arg,
    curl_event_request_t *req) {
    const char *source_file = (const char *)arg;
    if (success) {
        fprintf(log_file, "[SUCCESS] Source: %s, File: %s\n", source_file, filename);
    } else {
        fprintf(log_file, "[FAILURE] Source: %s, File: %s, CURLcode: %d, HTTP code: %ld, Error: %s\n",
                source_file, filename, result, http_code, error_msg ? error_msg : "Unknown error");
    }

    free(arg);
}

// --------------------------- MAIN ------------------------------------

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file> [prefix]\n", argv[0]);
        return EXIT_FAILURE;
    }

    log_file = fopen("download_results.log", "w");

    const char *input_file_path = argv[1];
    const char *prefix = (argc > 2) ? argv[2] : "";

    // Open input file
    FILE *input_file = fopen(input_file_path, "r");
    if (!input_file) {
        perror("Failed to open input file");
        return EXIT_FAILURE;
    }

    // Seed random for jitter
    srand((unsigned int)time(NULL));

    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_ALL);

    // Initialize the curl event loop
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    // Initialize the shared handle for DNS caching
    curl_event_plugin_gcloud_token_init(loop, "service-account.json", "gcloud_token", false);

    char line[2048];
    int file_number = 0;
    while (fgets(line, sizeof(line), input_file)) {
        // Parse line
        char url[1024], dest_file[1024];
        strcpy(url, prefix);
        int n = sscanf(line, "%1023s %1023s", url + strlen(prefix), dest_file);
        if (n < 1) {
            fprintf(stderr, "Malformed input: %s\n", line);
            continue;
        }
        if (n == 1) {
            snprintf(dest_file, sizeof(dest_file), "file_%d", file_number++);
        }

        // Initialize a file output with a logging callback
        curl_output_interface_t *output = file_output(dest_file, log_download_result, (void *)strdup(url));

        // Initialize a GCS download plugin
        curl_event_plugin_gcs_download_init(loop, GOOGLE_PROJECT_ID, url, "gcloud_token", output, 0);
    }

    fclose(input_file);
    fclose(log_file);

    // Run the event loop
    curl_event_loop_run(loop);

    // Clean up
    curl_event_loop_destroy(loop);
    curl_global_cleanup();

    return 0;
}
