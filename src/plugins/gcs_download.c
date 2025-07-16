// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcs_download.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GCS_STORAGE_URL_FORMAT = "https://storage.googleapis.com/%s/%s";

void swap_https_for_http(char *url) {
    if (!url) return;  // Safety check
    if (!strncmp(url, "https", 5)) { // Check if it's "https"
        for (char *p = url + 4; *p; ++p) { // Start shifting from 's'
            *p = *(p + 1);                 // Move characters left by one
        }
    }
}

/**
 * Prepare the request by setting the Authorization header and initializing the output interface.
 */
static bool gcs_on_prepare(curl_event_request_t *req) {
    // Get the access token from the event loop state
    char *access_token = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!access_token) {
        fprintf(stderr, "[gcs_download] Missing access token.\n");
        return false;
    }

    // Set the Authorization header
    char auth_header[512];
    sprintf(auth_header, "%s_metadata_flavor", req->dependencies[0]);
    char *metadata_flavor = curl_event_loop_get_state(req->loop, auth_header);
    if(metadata_flavor && !strcmp(metadata_flavor, "true")) {
        swap_https_for_http(req->url);
    }
    if(metadata_flavor)
        aml_free(metadata_flavor);

    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    aml_free(access_token);
    return true;
}

static size_t gcs_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    curl_output_interface_t *output = (curl_output_interface_t *)req->userdata;
    if(output) {
        if(!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (output && output->write) {
            return output->write(data, size, nmemb, output);
        }
    }
    return size * nmemb; // Default to consuming all data
}

/**
 * Handle successful download completion.
 */
static int gcs_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle; // Unused

    curl_output_interface_t *output = (curl_output_interface_t *)req->userdata;
    if(output) {
        if(!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (output && output->complete) {
            output->complete(output, req);
        }
    }

    // printf("[gcs_download] Download completed successfully.\n");
    return 0; // Request succeeded
}

/**
 * Handle download failure.
 */
static int gcs_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle; // Unused

    curl_output_interface_t *output = (curl_output_interface_t *)req->userdata;
    if(output) {
        if(!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (output->failure) {
            output->failure(result, http_code, output, req);
        }
    }
    fprintf(stderr, "[gcs_download] Download failed %s (CURLcode: %d, HTTP code: %ld).\n", req->url, result, http_code);
	if(http_code == 401) {
		// retry using default exp backoff
		return -1;
	}

    return 0; // Failure is not transient
}

/**
 * Clean up the output interface.
 */
static void gcs_output_destroy(void *userdata) {
    curl_output_interface_t *output = (curl_output_interface_t *)userdata;
    if (output && output->destroy) {
        output->destroy(output);
    }
}

bool curl_event_plugin_gcs_download_init(
    curl_event_loop_t *loop,
    const char *bucket,
    const char *object,
    const char *token_state_key,
    curl_output_interface_t *output_interface,
    long max_download_size
) {
    if (!loop || !bucket || !object || !token_state_key || !output_interface) {
        fprintf(stderr, "[gcs_download_init] Invalid arguments.\n");
        return false;
    }

    // Build the GCS URL
    char url[1024];
    snprintf(url, sizeof(url), "https://storage.googleapis.com/%s/%s", bucket, object);
    fprintf( stderr, "Downloading from: %s\n", url);
    // Prepare dependencies array (null-terminated)
    const char *dependencies[2] = { token_state_key, NULL };

    // Prepare the request
    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.method = "GET"; // Method does not need heap allocation
    req.dependencies = (char **)dependencies;

    req.write_cb = gcs_on_write;
    req.on_prepare = gcs_on_prepare;
    req.on_complete = gcs_on_complete;
    req.on_failure = gcs_on_failure;
    req.userdata_cleanup = gcs_output_destroy;
    req.userdata = output_interface;

    req.low_speed_limit = 1024; // 1 KB/s
    req.low_speed_time = 60;    // 60 seconds
    req.max_retries = 5;       // Retry up to 5 times
    req.max_download_size = max_download_size;

    // Enqueue the request
    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[gcs_download_init] Failed to enqueue request.\n");
        return false;
    }

    return true;
}