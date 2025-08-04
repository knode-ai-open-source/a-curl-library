// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcloud/cloudsql_query.h"
#include "a-curl-library/plugins/gcloud/token.h"   // gcloud_token_payload_t
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-memory-library/aml_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NOTE: This URL is illustrative; adjust to the real Admin API endpoint you use. */
static const char *CLOUDSQL_QUERY_URL_FORMAT =
    "https://sqladmin.googleapis.com/v1/projects/%s/instances/%s/databases/%s/executeQuery";

/* Per-request context */
typedef struct cloudsql_ctx_s {
    curl_event_res_id        token_id;      /* dependency: Google token resource */
    char                    *url;           /* owned */
    char                    *post_body;     /* owned */
    curl_output_interface_t *output;        /* not owned; destroyed via callback */
} cloudsql_ctx_t;

/* Destroy ctx and (optionally) output */
static void cloudsql_ctx_destroy(void *userdata) {
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->output && ctx->output->destroy) {
        ctx->output->destroy(ctx->output);
    }
    if (ctx->url)      aml_free(ctx->url);
    if (ctx->post_body) aml_free(ctx->post_body);
    aml_free(ctx);
}

/* Build headers & body before each attempt */
static bool cloudsql_on_prepare(curl_event_request_t *req) {
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)req->userdata;

    /* Token must be ready because we declared it as a dependency. */
    const gcloud_token_payload_t *tok =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, ctx->token_id);
    if (!tok || !tok->access_token) {
        fprintf(stderr, "[cloudsql_query] token not available (failed dep?)\n");
        return false;
    }

    /* Authorization: Bearer <token> */
    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", tok->access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");

    /* Ensure method/body/url are set (they already are from init, but harmless). */
    req->method    = "POST";
    req->post_data = ctx->post_body;
    req->url       = ctx->url;

    return true;
}

/* Stream response to output interface */
static size_t cloudsql_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)req->userdata;
    curl_output_interface_t *out = ctx ? ctx->output : NULL;
    if (out) {
        if (!req->output_initialized && out->init) {
            out->init(out, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (out->write) {
            return out->write(data, size, nmemb, out);
        }
    }
    return size * nmemb;
}

static int cloudsql_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)req->userdata;
    curl_output_interface_t *out = ctx ? ctx->output : NULL;

    if (out) {
        if (!req->output_initialized && out->init) {
            out->init(out, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (out->complete) out->complete(out, req);
    }

    fprintf(stderr, "[cloudsql_query] Query executed successfully.\n");
    return 0; /* done */
}

static int cloudsql_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)req->userdata;
    curl_output_interface_t *out = ctx ? ctx->output : NULL;

    if (out) {
        if (!req->output_initialized && out->init) {
            out->init(out, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (out->failure) out->failure(result, http_code, out, req);
    }

    fprintf(stderr, "[cloudsql_query] Failure (CURLcode=%d HTTP=%ld).\n",
            (int)result, http_code);
    return 0; /* not transient by default */
}

/* Public entry */
bool curl_event_plugin_cloudsql_query_init(
    curl_event_loop_t *loop,
    const char *instance_connection_name,  /* e.g. "my-project:us-central1:my-instance" */
    const char *database,
    curl_event_res_id  token_id,           /* dependency on gcloud_token resource */
    const char *query,
    curl_output_interface_t *output_interface
) {
    if (!loop || !instance_connection_name || !database || !query || !output_interface || token_id == 0) {
        fprintf(stderr, "[cloudsql_query_init] Invalid arguments.\n");
        return false;
    }

    /* Split instance_connection_name into project and instance if needed.
       For now assume caller passes project and instance separately or your
       format encodes them. Replace this with your real parsing. */
    const char *project = "your-project-id";     /* TODO: get from your config/env */
    const char *instance = instance_connection_name;

    /* Build URL */
    char url_buf[1024];
    snprintf(url_buf, sizeof(url_buf), CLOUDSQL_QUERY_URL_FORMAT,
             project, instance, database);

    /* Build JSON body (escape query properly in production) */
    char body_buf[2048];
    snprintf(body_buf, sizeof(body_buf), "{ \"query\": \"%s\" }", query);

    /* Allocate ctx */
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)aml_calloc(1, sizeof(*ctx));
    if (!ctx) {
        fprintf(stderr, "[cloudsql_query_init] OOM.\n");
        return false;
    }
    ctx->token_id  = token_id;
    ctx->url       = aml_strdup(url_buf);
    ctx->post_body = aml_strdup(body_buf);
    ctx->output    = output_interface;

    /* Prepare the request */
    curl_event_request_t req = {0};
    req.loop             = loop;
    req.url              = ctx->url;
    req.method           = "POST";
    req.post_data        = ctx->post_body;

    req.write_cb         = cloudsql_on_write;
    req.on_prepare       = cloudsql_on_prepare;
    req.on_complete      = cloudsql_on_complete;
    req.on_failure       = cloudsql_on_failure;

    req.userdata         = ctx;
    req.userdata_cleanup = cloudsql_ctx_destroy;

    req.connect_timeout  = 10;
    req.transfer_timeout = 60;
    req.max_retries      = 5;

    /* Declare dependency on the token resource */
    curl_event_request_depend(&req, token_id);

    /* Enqueue */
    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[cloudsql_query_init] Failed to enqueue request.\n");
        cloudsql_ctx_destroy(ctx);
        return false;
    }

    return true;
}
