// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/cloudsql_query.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *CLOUDSQL_QUERY_URL_FORMAT = "https://sqladmin.googleapis.com/v1/projects/%s/instances/%s/databases/%s/executeQuery";

/**
 * Prepare the request by setting the Authorization header, constructing the POST body, and initializing the output interface.
 */
static bool cloudsql_on_prepare(curl_event_request_t *req) {
    // Get the access token from the event loop state
    char *access_token = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!access_token) {
        fprintf(stderr, "[cloudsql_query] Missing access token.\n");
        return false;
    }

    // Set the Authorization header
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    aml_free(access_token);

    // Set Content-Type header for JSON request
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    return true;
}

static size_t cloudsql_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    curl_output_interface_t *output = (curl_output_interface_t *)req->userdata;
    if(output) {
        if(!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (output->write) {
            return output->write(data, size, nmemb, output);
        }
    }
    return size * nmemb; // Default to consuming all data
}

/**
 * Handle successful query execution.
 */
static int cloudsql_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle; // Unused

    curl_output_interface_t *output = (curl_output_interface_t *)req->userdata;
    if(output) {
        if(!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }

        if (output->complete) {
            output->complete(output, req);
        }
    }

    printf("[cloudsql_query] Query executed successfully.\n");
    return 0; // Request succeeded
}

/**
 * Handle query execution failure.
 */
static int cloudsql_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
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
    fprintf(stderr, "[cloudsql_query] Query execution failed (CURLcode: %d, HTTP code: %ld).\n", result, http_code);
    return 0; // Failure is not transient
}

/**
 * Clean up the output interface.
 */
static void cloudsql_output_destroy(void *userdata) {
    curl_output_interface_t *output = (curl_output_interface_t *)userdata;
    if (output && output->destroy) {
        output->destroy(output);
    }
}

bool curl_event_plugin_cloudsql_query_init(
    curl_event_loop_t *loop,
    const char *instance_connection_name,
    const char *database,
    const char *token_state_key,
    const char *query,
    curl_output_interface_t *output_interface
) {
    if (!loop || !instance_connection_name || !database || !token_state_key || !query || !output_interface) {
        fprintf(stderr, "[cloudsql_query_init] Invalid arguments.\n");
        return false;
    }

    // Build the Cloud SQL API URL
    char url[1024];
    snprintf(url, sizeof(url), CLOUDSQL_QUERY_URL_FORMAT, "your-project-id", instance_connection_name, database);

    // Construct the POST body for the query
    char post_body[2048];
    snprintf(post_body, sizeof(post_body), "{ \"query\": \"%s\" }", query);

    // Prepare dependencies array (null-terminated)
    const char *dependencies[2] = { token_state_key, NULL };

    // Prepare the request
    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.method = "POST"; // POST for executing queries
    req.post_data = post_body;
    req.dependencies = (char **)dependencies;

    req.write_cb = cloudsql_on_write;
    req.on_prepare = cloudsql_on_prepare;
    req.on_complete = cloudsql_on_complete;
    req.on_failure = cloudsql_on_failure;
    req.userdata_cleanup = cloudsql_output_destroy;
    req.userdata = output_interface;

    req.connect_timeout = 10;   // Connection timeout
    req.transfer_timeout = 60; // Transfer timeout
    req.max_retries = 5;       // Retry up to 5 times

    // Enqueue the request
    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[cloudsql_query_init] Failed to enqueue request.\n");
        return false;
    }

    return true;
}
