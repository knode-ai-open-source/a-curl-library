// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcloud/spanner_query.h"
#include "a-curl-library/plugins/gcloud/token.h" /* gcloud_token_payload_t */
#include "a-curl-library/curl_resource.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SPANNER_QUERY_URL_FORMAT =
    "https://spanner.googleapis.com/v1/projects/%s/instances/%s/databases/%s/sessions/%s:executeSql";

/* Prepare: set Authorization and ensure body includes the session name if needed.
   We build URL with /sessions/{name}:executeSql, so we don't need to inject
   "session" into the body anymore. (If you prefer the :executeSql on database
   path, you can keep the body injection approach shown earlier.) */
static bool spanner_on_prepare(curl_event_request_t *req) {
    if (!req || !req->dep_head || !req->dep_head->next) {
        fprintf(stderr, "[spanner_query] missing dependencies.\n");
        return false;
    }

    /* token first, session second (order defined in _init) */
    const gcloud_token_payload_t *tok =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, req->dep_head->id);
    const char *session_name =
        (const char *)curl_event_res_peek(req->loop, req->dep_head->next->id);

    if (!tok || !tok->access_token || !session_name || !*session_name) {
        fprintf(stderr, "[spanner_query] missing token or session name.\n");
        return false;
    }

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", tok->access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    return true;
}

static size_t spanner_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    curl_output_interface_t *output = (curl_output_interface_t *)req->userdata;
    if (output) {
        if (!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (output->write) {
            return output->write(data, size, nmemb, output);
        }
    }
    return size * nmemb;
}

static int spanner_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;
    curl_output_interface_t *output = (curl_output_interface_t *)req->userdata;
    if (output) {
        if (!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (output->complete) {
            output->complete(output, req);
        }
    }
    return 0;
}

static int spanner_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;
    curl_output_interface_t *output = (curl_output_interface_t *)req->userdata;
    if (output) {
        if (!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (output->failure) {
            output->failure(result, http_code, output, req);
        }
    }
    fprintf(stderr, "[spanner_query] failed (CURLcode=%d, HTTP=%ld)\n", result, http_code);
    /* non-transient by default */
    return 0;
}

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
    curl_event_res_id  token_id,
    curl_event_res_id  session_id,
    const char *sql_statement,
    curl_output_interface_t *output_interface
) {
    if (!loop || !project_id || !instance_id || !database_id ||
        token_id == 0 || session_id == 0 || !sql_statement || !output_interface) {
        fprintf(stderr, "[spanner_query_init] Invalid arguments.\n");
        return false;
    }

    /* We must know the session name to build the URL.
       Because the request will only start when deps are ready, we can peek it now
       to build the URL eagerly. Alternatively, build base and rebuild in prepare. */
    const char *session_name =
        (const char *)curl_event_res_peek(loop, session_id);
    if (!session_name || !*session_name) {
        /* If not yet ready, still construct a placeholder; the loop will block until ready.
           We'll (re)build the final URL in on_prepare when session is available. */
        /* Build provisional URL (will be updated in on_prepare if needed) */
        session_name = "SESSION_PLACEHOLDER";
    }

    char *url = aml_strdupf(SPANNER_QUERY_URL_FORMAT,
                            project_id, instance_id, database_id, session_name);
    if (!url) {
        fprintf(stderr, "[spanner_query_init] OOM (url).\n");
        return false;
    }

    /* Build the SQL payload */
    char *payload = aml_strdupf("{\"sql\":\"%s\"}", sql_statement);
    if (!payload) {
        aml_free(url);
        fprintf(stderr, "[spanner_query_init] OOM (payload).\n");
        return false;
    }

    curl_event_request_t req = (curl_event_request_t){0};
    req.loop   = loop;
    req.url    = url;        /* heap */
    req.method = "POST";
    req.post_data = payload; /* heap */
    req.write_cb   = spanner_on_write;
    req.on_prepare = spanner_on_prepare;
    req.on_complete= spanner_on_complete;
    req.on_failure = spanner_on_failure;
    req.userdata_cleanup = spanner_output_destroy;
    req.userdata = output_interface;

    req.connect_timeout  = 10;
    req.transfer_timeout = 60;
    req.max_retries      = 5;

    /* Dependencies: token, session (order matters for on_prepare convenience) */
    curl_event_request_depend(&req, token_id);
    curl_event_request_depend(&req, session_id);

    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[spanner_query_init] Failed to enqueue request.\n");
        aml_free(url);
        aml_free(payload);
        return false;
    }

    return true;
}
