// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcloud/spanner_session.h"
#include "a-curl-library/plugins/gcloud/token.h" /* gcloud_token_payload_t */
#include "a-curl-library/curl_resource.h"
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

    curl_event_res_id token_id;
    curl_event_res_id session_id;

    char  *response_buffer;
    size_t response_len;
    size_t response_capacity;
} curl_event_plugin_spanner_session_t;

/**
 * Prepare: set Authorization from gcloud_token payload.
 */
static bool spanner_session_on_prepare(curl_event_request_t *req) {
    curl_event_plugin_spanner_session_t *plugin =
        (curl_event_plugin_spanner_session_t *)req->userdata;

    const gcloud_token_payload_t *p =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, plugin->token_id);
    if (!p || !p->access_token) {
        fprintf(stderr, "[spanner_session] Missing access token.\n");
        return false;
    }

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", p->access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    return true;
}

/**
 * Write callback to store the response data.
 */
static size_t spanner_session_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    curl_event_plugin_spanner_session_t *plugin =
        (curl_event_plugin_spanner_session_t *)req->userdata;

    size_t total = size * nmemb;

    if (plugin->response_len + total + 1 > plugin->response_capacity) {
        size_t new_capacity = (plugin->response_len + total + 1) * 2;
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
 * Handle successful session creation: publish session name.
 */
static int spanner_session_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;
    curl_event_plugin_spanner_session_t *plugin =
        (curl_event_plugin_spanner_session_t *)req->userdata;

    aml_pool_t *pool = aml_pool_init(1024);
    ajson_t *json_response = ajson_parse_string(pool, plugin->response_buffer);
    plugin->response_len = 0; // Reset for any reuse

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

    /* Publish the session name under the provided session_id */
    curl_event_res_publish_str(req->loop, plugin->session_id, session_name);
    fprintf(stderr, "[spanner_session] Session created: %s\n", session_name);

    aml_pool_destroy(pool);
    return 0;
}

/**
 * Handle session creation failure (non-transient by default).
 */
static int spanner_session_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;
    fprintf(stderr, "[spanner_session] Session creation failed (CURLcode: %d, HTTP code: %ld).\n",
            result, http_code);
    return 0;
}

/**
 * Destroy plugin resources.
 */
static void spanner_session_destroy(curl_event_plugin_spanner_session_t *plugin) {
    if (!plugin) return;
    aml_free(plugin->project_id);
    aml_free(plugin->instance_id);
    aml_free(plugin->database_id);
    aml_free(plugin->response_buffer);
    aml_free(plugin);
}

bool curl_event_plugin_spanner_session_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *instance_id,
    const char *database_id,
    curl_event_res_id  token_id,
    curl_event_res_id  session_id
) {
    if (!loop || !project_id || !instance_id || !database_id || token_id == 0 || session_id == 0) {
        fprintf(stderr, "[spanner_session_init] Invalid arguments.\n");
        return false;
    }

    /* Alloc plugin */
    curl_event_plugin_spanner_session_t *plugin =
        (curl_event_plugin_spanner_session_t *)aml_calloc(1, sizeof(*plugin));
    if (!plugin) {
        fprintf(stderr, "[spanner_session_init] OOM.\n");
        return false;
    }

    plugin->loop        = loop;
    plugin->project_id  = aml_strdup(project_id);
    plugin->instance_id = aml_strdup(instance_id);
    plugin->database_id = aml_strdup(database_id);
    plugin->token_id    = token_id;
    plugin->session_id  = session_id;
    plugin->response_capacity = 1024;
    plugin->response_buffer   = (char *)aml_calloc(1, plugin->response_capacity);

    /* Build URL / body on heap */
    char *url  = aml_strdupf(SPANNER_SESSION_URL_FORMAT, project_id, instance_id, database_id);
    char *body = aml_strdup("{}");
    if (!url || !body) {
        aml_free(url); aml_free(body);
        spanner_session_destroy(plugin);
        fprintf(stderr, "[spanner_session_init] OOM (url/body).\n");
        return false;
    }

    /* Build request */
    curl_event_request_t req = (curl_event_request_t){0};
    req.loop   = loop;
    req.url    = url;   /* loop owns on success */
    req.method = "POST";
    req.post_data = body; /* loop owns on success */
    req.write_cb   = spanner_session_on_write;
    req.on_prepare = spanner_session_on_prepare;
    req.on_complete= spanner_session_on_complete;
    req.on_failure = spanner_session_on_failure;
    req.userdata_cleanup = (curl_event_cleanup_userdata_t)spanner_session_destroy;
    req.userdata = plugin;

    req.connect_timeout  = 10;
    req.transfer_timeout = 60;
    req.max_retries      = 3;

    /* Depend on the token resource */
    curl_event_request_depend(&req, token_id);

    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[spanner_session_init] enqueue failed.\n");
        aml_free(url);
        aml_free(body);
        spanner_session_destroy(plugin);
        return false;
    }

    return true;
}
