// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/spanner_session.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SPANNER_SESSION_URL_FORMAT =
    "https://spanner.googleapis.com/v1/projects/%s/instances/%s/databases/%s/sessions";

/* =======================
   Internal struct
   ======================= */
typedef struct curl_event_plugin_spanner_session_s {
    curl_event_loop_t *loop;
    char *project_id;
    char *instance_id;
    char *database_id;
    char *token_state_key;
    char *session_state_key;

    char *response_buffer;
    size_t response_len;
    size_t response_capacity;

    int refresh_attempts;
} curl_event_plugin_spanner_session_t;

/**
 * Prepare the session creation request by setting the Authorization header.
 */
static bool spanner_session_on_prepare(curl_event_request_t *req) {
    curl_event_plugin_spanner_session_t *plugin = (curl_event_plugin_spanner_session_t *)req->userdata;

    // Get the access token from the event loop state
    char *access_token = curl_event_loop_get_state(req->loop, plugin->token_state_key);
    if (!access_token) {
        fprintf(stderr, "[spanner_session] Missing access token.\n");
        return false;
    }

    // Set the Authorization header
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    aml_free(access_token);

    // Set Content-Type header for JSON requests
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    return true;
}

/**
 * Write callback to store the response data.
 */
static size_t spanner_session_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    curl_event_plugin_spanner_session_t *plugin = (curl_event_plugin_spanner_session_t *)req->userdata;

    size_t total = size * nmemb;

    if (plugin->response_len + total + 1 > plugin->response_capacity) {
        size_t new_capacity = plugin->response_len + total + 1;
        char *new_buffer = (char *)realloc(plugin->response_buffer, new_capacity);
        if (!new_buffer) {
            fprintf(stderr, "[spanner_session] Memory allocation failed.\n");
            return 0; // Indicate failure
        }
        plugin->response_buffer = new_buffer;
        plugin->response_capacity = new_capacity;
    }

    memcpy(plugin->response_buffer + plugin->response_len, data, total);
    plugin->response_len += total;
    plugin->response_buffer[plugin->response_len] = '\0';

    return total;
}

/**
 * Handle successful session creation.
 */
static int spanner_session_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    curl_event_plugin_spanner_session_t *plugin = (curl_event_plugin_spanner_session_t *)req->userdata;

    // Parse the response to extract the session name
    aml_pool_t *pool = aml_pool_init(1024);
    ajson_t *json_response = ajson_parse_string(pool, plugin->response_buffer);
    plugin->response_len = 0; // Reset for future use

    if (ajson_is_error(json_response)) {
        fprintf(stderr, "[spanner_session] Failed to parse session creation response.\n");
        aml_pool_destroy(pool);
        return 2; // retry in 2 seconds
    }

    char *session_name = ajson_extract_string(pool, ajsono_scan(json_response, "name"));
    if (!session_name) {
        fprintf(stderr, "[spanner_session] Missing session name in response.\n");
        aml_pool_destroy(pool);
        return 2; // retry in 2 seconds
    }

    // Save the session name in the loop's state
    curl_event_loop_put_state(req->loop, plugin->session_state_key, session_name);

    printf("[spanner_session] Session created: %s\n", session_name);

    aml_pool_destroy(pool);
    return 0;
}

/**
 * Handle session creation failure.
 */
static int spanner_session_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;

    curl_event_plugin_spanner_session_t *plugin = (curl_event_plugin_spanner_session_t *)req->userdata;
    fprintf(stderr, "[spanner_session] Session creation failed (CURLcode: %d, HTTP code: %ld).\n", result, http_code);
    return 0; // Failure is not transient
}

/**
 * Destroy the plugin's allocated resources.
 */
static void spanner_session_destroy(curl_event_plugin_spanner_session_t *plugin) {
    if (!plugin) return;
    aml_free(plugin->project_id);
    aml_free(plugin->instance_id);
    aml_free(plugin->database_id);
    aml_free(plugin->token_state_key);
    aml_free(plugin->session_state_key);
    aml_free(plugin->response_buffer);
    aml_free(plugin);
}

bool curl_event_plugin_spanner_session_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *instance_id,
    const char *database_id,
    const char *token_state_key,
    const char *session_state_key
) {
    if (!loop || !project_id || !instance_id || !database_id || !token_state_key || !session_state_key) {
        fprintf(stderr, "[spanner_session_init] Invalid arguments.\n");
        return false;
    }

    // Allocate and initialize the plugin struct
    curl_event_plugin_spanner_session_t *plugin =
        (curl_event_plugin_spanner_session_t *)aml_calloc(1, sizeof(*plugin));
    if (!plugin) {
        fprintf(stderr, "[spanner_session_init] Memory allocation failed.\n");
        return false;
    }

    plugin->loop = loop;
    plugin->project_id = strdup(project_id);
    plugin->instance_id = strdup(instance_id);
    plugin->database_id = strdup(database_id);
    plugin->token_state_key = strdup(token_state_key);
    plugin->session_state_key = strdup(session_state_key);
    plugin->response_capacity = 512;
    plugin->response_buffer = (char *)aml_calloc(1,plugin->response_capacity);

    // Build the Spanner session URL
    char url[1024];
    snprintf(url, sizeof(url), SPANNER_SESSION_URL_FORMAT, project_id, instance_id, database_id);
    printf( "Session URL: %s\n", url);
    // Prepare dependencies array (null-terminated)
    const char *dependencies[2] = { token_state_key, NULL };

    // Prepare the request
    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.method = "POST"; // Create session uses POST
    req.post_data = "{}"; // Empty JSON payload
    req.write_cb = spanner_session_on_write;
    req.on_prepare = spanner_session_on_prepare;
    req.on_complete = spanner_session_on_complete;
    req.on_failure = spanner_session_on_failure;
    req.userdata_cleanup = (curl_event_cleanup_userdata_t)spanner_session_destroy;
    req.userdata = plugin;
    req.dependencies = (char **)dependencies;

    req.connect_timeout = 10;   // Connection timeout
    req.transfer_timeout = 60; // Transfer timeout
    req.max_retries = 3;       // Retry up to 3 times

    // Enqueue the request
    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[spanner_session_init] Failed to enqueue session creation request.\n");
        spanner_session_destroy(plugin);
        return false;
    }

    return true;
}
