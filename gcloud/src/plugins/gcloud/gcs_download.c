// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcloud/gcs_download.h"
#include "a-curl-library/plugins/gcloud/token.h"   // gcloud_token_payload_t
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_event_request.h"
#include "a-memory-library/aml_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GCS_STORAGE_URL_FORMAT = "https://storage.googleapis.com/%s/%s";

/* Simple in-place https->http (removes the 's'). Requires mutable string. */
static void swap_https_for_http(char *url) {
    if (!url) return;
    if (strncmp(url, "https", 5) == 0) {
        for (char *p = url + 4; *p; ++p) {
            *p = *(p + 1);
        }
    }
}

/* Per-request context */
typedef struct gcs_ctx_s {
    curl_event_res_id        token_id;   /* dependency: Google token resource */
    char                    *url;        /* owned, mutable for https->http swap */
    curl_output_interface_t *output;     /* not owned; destroyed via callback */
} gcs_ctx_t;

static void gcs_ctx_destroy(void *userdata) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->output && ctx->output->destroy) {
        ctx->output->destroy(ctx->output);
    }
    if (ctx->url) aml_free(ctx->url);
    aml_free(ctx);
}

/* Prepare: add Authorization header; possibly swap https->http for metadata */
static bool gcs_on_prepare(curl_event_request_t *req) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)req->userdata;

    const gcloud_token_payload_t *tok =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, ctx->token_id);
    if (!tok || !tok->access_token) {
        fprintf(stderr, "[gcs_download] Missing token payload (dep not ready/failed).\n");
        return false;
    }

    /* If metadata token, talk to metadata network over http (no TLS) */
    if (tok->metadata_flavor) {
        swap_https_for_http(ctx->url);
    }
    req->url = ctx->url;

    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", tok->access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);

    /* GET with optional size/speed limits already set in init */
    return true;
}

static size_t gcs_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)req->userdata;
    curl_output_interface_t *output = ctx ? ctx->output : NULL;

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

static int gcs_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;

    gcs_ctx_t *ctx = (gcs_ctx_t *)req->userdata;
    curl_output_interface_t *output = ctx ? ctx->output : NULL;

    if (output) {
        if (!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (output->complete) {
            output->complete(output, req);
        }
    }
    return 0; /* success */
}

static int gcs_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;

    gcs_ctx_t *ctx = (gcs_ctx_t *)req->userdata;
    curl_output_interface_t *output = ctx ? ctx->output : NULL;

    if (output) {
        if (!req->output_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->output_initialized = true;
        }
        if (output->failure) {
            output->failure(result, http_code, output, req);
        }
    }

    fprintf(stderr, "[gcs_download] Download failed %s (CURLcode:%d, HTTP:%ld)\n",
            req->url ? req->url : "(null)", (int)result, http_code);

    if (http_code == 401) {
        /* Token expired or invalid: let scheduler use default backoff. */
        return -1;
    }
    return 0; /* non-transient by default */
}

bool curl_event_plugin_gcs_download_init(
    curl_event_loop_t *loop,
    const char *bucket,
    const char *object,
    curl_event_res_id  token_id,
    curl_output_interface_t *output_interface,
    long max_download_size
) {
    if (!loop || !bucket || !object || token_id == 0 || !output_interface) {
        fprintf(stderr, "[gcs_download_init] Invalid arguments.\n");
        return false;
    }

    /* Build URL and keep a heap copy we can mutate for http swap */
    char url_buf[1024];
    snprintf(url_buf, sizeof(url_buf), GCS_STORAGE_URL_FORMAT, bucket, object);

    gcs_ctx_t *ctx = (gcs_ctx_t *)aml_calloc(1, sizeof(*ctx));
    if (!ctx) {
        fprintf(stderr, "[gcs_download_init] OOM.\n");
        return false;
    }
    ctx->token_id = token_id;
    ctx->url      = aml_strdup(url_buf);
    ctx->output   = output_interface;

    /* Build request */
    curl_event_request_t req = {0};
    req.loop              = loop;
    req.url               = ctx->url;         /* may be swapped to http in on_prepare */
    req.method            = "GET";
    req.write_cb          = gcs_on_write;
    req.on_prepare        = gcs_on_prepare;
    req.on_complete       = gcs_on_complete;
    req.on_failure        = gcs_on_failure;
    req.userdata          = ctx;
    req.userdata_cleanup  = gcs_ctx_destroy;

    /* Download tuning */
    req.low_speed_limit   = 1024;   /* 1 KB/s */
    req.low_speed_time    = 60;     /* 60 seconds */
    req.max_retries       = 5;
    req.max_download_size = max_download_size;

    /* Block on token availability */
    curl_event_request_depend(&req, token_id);

    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[gcs_download_init] Failed to enqueue request.\n");
        gcs_ctx_destroy(ctx);
        return false;
    }

    return true;
}
