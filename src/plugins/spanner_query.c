// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/spanner_query.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SPANNER_QUERY_URL_FORMAT =
    "https://spanner.googleapis.com/v1/projects/%s/instances/%s/databases/%s:executeSql";

/**
 * Prepare the request by setting the Authorization header and including the session in the payload.
 */
static bool spanner_on_prepare(curl_event_request_t *req) {
    // Get the access token from the event loop state
    char *access_token = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!access_token) {
        fprintf(stderr, "[spanner_query] Missing access token.\n");
        return false;
    }

    // Get the session ID from the event loop state
    char *session_id = curl_event_loop_get_state(req->loop, req->dependencies[1]);
    if (!session_id) {
        fprintf(stderr, "[spanner_query] Missing session ID.\n");
        aml_free(access_token);
        return false;
    }

    // Set the Authorization header
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    aml_free(access_token);

    // Set Content-Type header for JSON requests
    curl_event_loop_update_header(req, "Content-Type", "application/json");

    // Include the session ID in the SQL payload
    char *updated_payload;
    size_t updated_payload_size = snprintf(
        NULL,
        0,
        "{\"session\":\"%s\",%s",
        session_id,
        req->post_data + 1 // Skip the initial '{' in the original payload
    ) + 1;
    updated_payload = (char *)aml_calloc(1,updated_payload_size);
    if (!updated_payload) {
        fprintf(stderr, "[spanner_query] Memory allocation failed.\n");
        aml_free(session_id);
        return false;
    }
    snprintf(
        updated_payload,
        updated_payload_size,
        "{\"session\":\"%s\",%s",
        session_id,
        req->post_data + 1 // Skip the initial '{' in the original payload
    );
    aml_free(session_id);
    aml_free(req->post_data); // Free the original payload
    req->post_data = updated_payload;

    printf( "URL: %s\nPost: %s\n", req->url, req->post_data);
    return true;
}

/**
 * Write callback to forward response data to the output interface.
 */
static size_t spanner_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
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
 * Handle successful query completion.
 */
static int spanner_on_complete(CURL *easy_handle, curl_event_request_t *req) {
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
    printf("[spanner_query] Query completed successfully.\n");
    return 0; // Request succeeded
}

/**
 * Handle query failure.
 */
static int spanner_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
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
    fprintf(stderr, "[spanner_query] Query failed (CURLcode: %d, HTTP code: %ld).\n", result, http_code);
    return 0; // Failure is not transient
}

/**
 * Clean up the output interface.
 */
static void spanner_output_destroy(void *userdata) {
    curl_output_interface_t *output = (curl_output_interface_t *)userdata;
    if (output && output->destroy) {
        output->destroy(output);
    }
}

bool curl_event_plugin_spanner_query_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *instance_id,
    const char *database_id,
    const char *token_state_key,
    const char *session_state_key,
    const char *sql_statement,
    curl_output_interface_t *output_interface
) {
    if (!loop || !project_id || !instance_id || !database_id || !token_state_key || !session_state_key || !sql_statement || !output_interface) {
        fprintf(stderr, "[spanner_query_init] Invalid arguments.\n");
        return false;
    }

    // Build the Spanner query URL
    char url[1024];
    snprintf(url, sizeof(url), SPANNER_QUERY_URL_FORMAT, project_id, instance_id, database_id);

    // Prepare the SQL payload in JSON format
    char *payload;
    size_t payload_size = snprintf(NULL, 0, "{\"sql\":\"%s\"}", sql_statement) + 1;
    payload = (char *)aml_calloc(1,payload_size);
    if (!payload) {
        fprintf(stderr, "[spanner_query_init] Memory allocation failed.\n");
        return false;
    }
    snprintf(payload, payload_size, "{\"sql\":\"%s\"}", sql_statement);

    // Prepare dependencies array (null-terminated)
    const char *dependencies[3] = { token_state_key, session_state_key, NULL };

    // Prepare the request
    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.method = "POST"; // Spanner queries use POST
    req.post_data = payload; // Set the SQL statement as the POST body
    req.dependencies = (char **)dependencies;

    req.write_cb = spanner_on_write;
    req.on_prepare = spanner_on_prepare;
    req.on_complete = spanner_on_complete;
    req.on_failure = spanner_on_failure;
    req.userdata_cleanup = spanner_output_destroy;
    req.userdata = output_interface;

    req.connect_timeout = 10;   // Connection timeout
    req.transfer_timeout = 60; // Transfer timeout
    req.max_retries = 5;       // Retry up to 5 times

    // Enqueue the request
    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[spanner_query_init] Failed to enqueue request.\n");
        aml_free(payload); // Free the payload on failure
        return false;
    }

    return true;
}