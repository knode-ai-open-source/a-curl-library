// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcloud/pubsub_pull.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/plugins/gcloud/token.h" /* gcloud_token_payload_t */
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PUBSUB_PULL_URL_FORMAT "https://pubsub.googleapis.com/v1/projects/%s/subscriptions/%s:pull"
#define PUBSUB_ACK_URL_FORMAT  "https://pubsub.googleapis.com/v1/projects/%s/subscriptions/%s:acknowledge"
#define PUBSUB_SEEK_URL_FORMAT "https://pubsub.googleapis.com/v1/projects/%s/subscriptions/%s:seek"

/* Use the first dependency (token_id) to peek gcloud_token_payload_t */
static bool pubsub_on_prepare(curl_event_request_t *req) {
    if (!req || !req->dep_head) {
        fprintf(stderr, "[pubsub] missing dependency\n");
        return false;
    }
    const gcloud_token_payload_t *p =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, req->dep_head->id);
    if (!p || !p->access_token || !*p->access_token) {
        fprintf(stderr, "[pubsub] Missing access token.\n");
        return false;
    }

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", p->access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    return true;
}

static size_t pubsub_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
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

static int pubsub_on_complete(CURL *easy_handle, curl_event_request_t *req) {
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

static int pubsub_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
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
    /* retry with default backoff on transient errors */
    return -1;
}

static void pubsub_output_destroy(void *userdata) {
    curl_output_interface_t *output = (curl_output_interface_t *)userdata;
    if (output && output->destroy) {
        output->destroy(output);
    }
}

/* Normalize subscription id to short form if a full path was provided */
static const char *normalize_sub_id(const char *subscription_id) {
    const char *rp = strrchr(subscription_id, '/');
    return rp ? rp + 1 : subscription_id;
}

bool curl_event_plugin_pubsub_pull_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    int max_messages,
    curl_output_interface_t *output_interface
) {
    if (!loop || !project_id || !subscription_id || token_id == 0 || !output_interface) {
        fprintf(stderr, "[pubsub_pull_init] Invalid arguments.\n");
        return false;
    }

    subscription_id = normalize_sub_id(subscription_id);

    char *url  = aml_strdupf(PUBSUB_PULL_URL_FORMAT, project_id, subscription_id);
    char *body = aml_strdupf("{\"maxMessages\":%d,\"returnImmediately\":false}", max_messages);
    if (!url || !body) {
        aml_free(url);
        aml_free(body);
        fprintf(stderr, "[pubsub_pull_init] OOM.\n");
        return false;
    }

    curl_event_request_t req = (curl_event_request_t){0};
    req.loop = loop;
    req.url  = url;   /* heap */
    req.method = "POST";
    req.post_data = body; /* heap */
    req.write_cb   = pubsub_on_write;
    req.on_prepare = pubsub_on_prepare;
    req.on_complete= pubsub_on_complete;
    req.on_failure = pubsub_on_failure;
    req.userdata_cleanup = pubsub_output_destroy;
    req.userdata  = output_interface;

    req.connect_timeout  = 10;
    req.transfer_timeout = 60;
    req.max_retries      = 3;

    /* Add token dependency (gcloud_token payload) */
    curl_event_request_depend(&req, token_id);

    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[pubsub_pull_init] Failed to enqueue request.\n");
        /* request not adopted; free heap strings */
        aml_free(url);
        aml_free(body);
        return false;
    }

    return true;
}

bool curl_event_plugin_pubsub_ack_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    const char **ack_ids,
    size_t num_ack_ids
) {
    if (!loop || !project_id || !subscription_id || token_id == 0 || !ack_ids || num_ack_ids == 0) {
        fprintf(stderr, "[pubsub_ack_init] Invalid arguments.\n");
        return false;
    }

    subscription_id = normalize_sub_id(subscription_id);

    char *url = aml_strdupf(PUBSUB_ACK_URL_FORMAT, project_id, subscription_id);
    if (!url) {
        fprintf(stderr, "[pubsub_ack_init] OOM (url).\n");
        return false;
    }

    /* Build {"ackIds":["id1","id2",...]} */
    size_t body_cap = 16; /* base */
    for (size_t i = 0; i < num_ack_ids; i++) body_cap += strlen(ack_ids[i]) + 3;
    char *body = (char *)aml_calloc(1, body_cap + 16);
    if (!body) { aml_free(url); fprintf(stderr, "[pubsub_ack_init] OOM (body).\n"); return false; }

    char *p = body;
    p += sprintf(p, "{\"ackIds\":[");
    for (size_t i = 0; i < num_ack_ids; i++) {
        p += sprintf(p, "\"%s\",", ack_ids[i]);
    }
    if (num_ack_ids > 0) p[-1] = ']'; else *p++ = ']';
    *p++ = '}';
    *p   = '\0';

    curl_event_request_t req = (curl_event_request_t){0};
    req.loop = loop;
    req.url  = url;      /* heap */
    req.method = "POST";
    req.post_data = body;/* heap */
    req.write_cb   = pubsub_on_write;
    req.on_prepare = pubsub_on_prepare;
    req.on_complete= pubsub_on_complete;
    req.on_failure = pubsub_on_failure;
    req.userdata_cleanup = pubsub_output_destroy;
    req.userdata  = NULL;

    req.connect_timeout  = 10;
    req.transfer_timeout = 60;
    req.max_retries      = 3;

    curl_event_request_depend(&req, token_id);

    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[pubsub_ack_init] Failed to enqueue request.\n");
        aml_free(url);
        aml_free(body);
        return false;
    }

    return true;
}

static bool curl_event_plugin_pubsub_seek_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    const char *timestamp,  /* RFC3339 or NULL */
    const char *snapshot    /* or NULL */
) {
    if (!loop || !project_id || !subscription_id || token_id == 0 || (!timestamp && !snapshot)) {
        fprintf(stderr, "[pubsub_seek_init] Invalid arguments.\n");
        return false;
    }

    subscription_id = normalize_sub_id(subscription_id);

    char *url = aml_strdupf(PUBSUB_SEEK_URL_FORMAT, project_id, subscription_id);
    if (!url) { fprintf(stderr, "[pubsub_seek_init] OOM (url).\n"); return false; }

    char *body = NULL;
    if (timestamp)      body = aml_strdupf("{\"time\":\"%s\"}", timestamp);
    else if (snapshot)  body = aml_strdupf("{\"snapshot\":\"%s\"}", snapshot);

    if (!body) { aml_free(url); fprintf(stderr, "[pubsub_seek_init] OOM (body).\n"); return false; }

    curl_event_request_t req = (curl_event_request_t){0};
    req.loop = loop;
    req.url  = url;       /* heap */
    req.method = "POST";
    req.post_data = body; /* heap */
    req.write_cb   = pubsub_on_write;
    req.on_prepare = pubsub_on_prepare;
    req.on_complete= pubsub_on_complete;
    req.on_failure = pubsub_on_failure;

    req.connect_timeout  = 10;
    req.transfer_timeout = 60;
    req.max_retries      = 3;

    curl_event_request_depend(&req, token_id);

    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[pubsub_seek_init] Failed to enqueue seek request.\n");
        aml_free(url);
        aml_free(body);
        return false;
    }

    return true;
}

bool curl_event_plugin_pubsub_seek_to_timestamp_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    const char *timestamp
) {
    return curl_event_plugin_pubsub_seek_init(loop, project_id, subscription_id, token_id, timestamp, NULL);
}

bool curl_event_plugin_pubsub_seek_to_snapshot_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    const char *snapshot
) {
    return curl_event_plugin_pubsub_seek_init(loop, project_id, subscription_id, token_id, NULL, snapshot);
}
