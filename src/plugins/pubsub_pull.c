// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/pubsub_pull.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PUBSUB_PULL_URL_FORMAT "https://pubsub.googleapis.com/v1/projects/%s/subscriptions/%s:pull"
#define PUBSUB_ACK_URL_FORMAT  "https://pubsub.googleapis.com/v1/projects/%s/subscriptions/%s:acknowledge"

static bool pubsub_on_prepare(curl_event_request_t *req) {
    char *access_token = curl_event_loop_get_state(req->loop, req->dependencies[0]);
    if (!access_token) {
        fprintf(stderr, "[pubsub] Missing access token.\n");
        return false;
    }

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    curl_event_loop_update_header(req, "Authorization", auth_header);
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    aml_free(access_token);
    return true;
}

static size_t pubsub_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
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
    return size * nmemb;
}

static int pubsub_on_complete(CURL *easy_handle, curl_event_request_t *req) {
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
    return 0;
}

static int pubsub_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
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
    return -1;
}

static void pubsub_output_destroy(void *userdata) {
    curl_output_interface_t *output = (curl_output_interface_t *)userdata;
    if (output && output->destroy) {
        output->destroy(output);
    }
}

bool curl_event_plugin_pubsub_pull_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    int max_messages,
    curl_output_interface_t *output_interface
) {
    if (!loop || !project_id || !subscription_id || !token_state_key || !output_interface) {
        fprintf(stderr, "[pubsub_pull_init] Invalid arguments.\n");
        return false;
    }

    char *rp = strrchr(subscription_id, '/');
    if(rp) {
        subscription_id = rp+1;
    }

    char url[1024];
    snprintf(url, sizeof(url), PUBSUB_PULL_URL_FORMAT, project_id, subscription_id);

    char body[128];
    snprintf(body, sizeof(body), "{\"maxMessages\":%d,\"returnImmediately\":false}", max_messages);

    const char *dependencies[2] = { token_state_key, NULL };

    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.method = "POST";
    req.post_data = body;
    req.dependencies = (char **)dependencies;

    req.write_cb = pubsub_on_write;
    req.on_prepare = pubsub_on_prepare;
    req.on_complete = pubsub_on_complete;
    req.on_failure = pubsub_on_failure;
    req.userdata_cleanup = pubsub_output_destroy;
    req.userdata = output_interface;

    req.connect_timeout = 10;
    req.transfer_timeout = 60;
    req.max_retries = 3;

    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[pubsub_pull_init] Failed to enqueue request.\n");
        return false;
    }

    return true;
}

bool curl_event_plugin_pubsub_ack_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    const char **ack_ids,
    size_t num_ack_ids
) {
    if (!loop || !project_id || !subscription_id || !token_state_key || !ack_ids || num_ack_ids == 0) {
        fprintf(stderr, "[pubsub_ack_init] Invalid arguments.\n");
        return false;
    }

    char *rp = strrchr(subscription_id, '/');
    if(rp) {
        subscription_id = rp+1;
    }

    char url[1024];
    snprintf(url, sizeof(url), PUBSUB_ACK_URL_FORMAT, project_id, subscription_id);

    size_t body_size = 16; // {"ackIds":[]}
    for (size_t i = 0; i < num_ack_ids; i++) {
        body_size += strlen(ack_ids[i]) + 3; // "ackId",
    }
    char *body = aml_calloc(1,body_size);
    if (!body) {
        fprintf(stderr, "[pubsub_ack_init] Memory allocation failed.\n");
        return false;
    }

    char *p = body;
    p += sprintf(p, "{\"ackIds\":[");
    for (size_t i = 0; i < num_ack_ids; i++) {
        p += sprintf(p, "\"%s\",", ack_ids[i]);
    }
    p[-1] = ']';
    p[0] = '}';
    p[1] = '\0';

    const char *dependencies[2] = { token_state_key, NULL };

    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.method = "POST";
    req.post_data = body;
    req.dependencies = (char **)dependencies;

    req.write_cb = pubsub_on_write;
    req.on_prepare = pubsub_on_prepare;
    req.on_complete = pubsub_on_complete;
    req.on_failure = pubsub_on_failure;
    req.userdata_cleanup = pubsub_output_destroy;
    req.userdata = NULL;

    req.connect_timeout = 10;
    req.transfer_timeout = 60;
    req.max_retries = 3;

    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[pubsub_ack_init] Failed to enqueue request.\n");
        aml_free(body);
        return false;
    }

    aml_free(body);
    return true;
}


static bool curl_event_plugin_pubsub_seek_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    const char *timestamp,
    const char *snapshot
) {
    if (!loop || !project_id || !subscription_id || !token_state_key || (!timestamp && !snapshot)) {
        fprintf(stderr, "[pubsub_seek_init] Invalid arguments.\n");
        return false;
    }

    char *rp = strrchr(subscription_id, '/');
    if(rp) {
        subscription_id = rp+1;
    }

    char url[1024];
    snprintf(url, sizeof(url), "https://pubsub.googleapis.com/v1/projects/%s/subscriptions/%s:seek", project_id, subscription_id);

    char body[512];
    if (timestamp) {
        snprintf(body, sizeof(body), "{\"time\":\"%s\"}", timestamp);
    } else if (snapshot) {
        snprintf(body, sizeof(body), "{\"snapshot\":\"%s\"}", snapshot);
    } else {
        fprintf(stderr, "[pubsub_seek_init] Either timestamp or snapshot must be provided.\n");
        return false;
    }

    const char *dependencies[2] = { token_state_key, NULL };

    curl_event_request_t req = {0};
    req.loop = loop;
    req.url = url;
    req.method = "POST";
    req.post_data = body;
    req.dependencies = (char **)dependencies;

    req.write_cb = pubsub_on_write;
    req.on_prepare = pubsub_on_prepare;
    req.on_complete = pubsub_on_complete;
    req.on_failure = pubsub_on_failure;

    req.connect_timeout = 10;
    req.transfer_timeout = 60;
    req.max_retries = 3;

    if (!curl_event_loop_enqueue(loop, &req, 0)) {
        fprintf(stderr, "[pubsub_seek_init] Failed to enqueue seek request.\n");
        return false;
    }

    return true;
}

bool curl_event_plugin_pubsub_seek_to_timestamp_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    const char *timestamp
) {
    return curl_event_plugin_pubsub_seek_init(loop, project_id, subscription_id, token_state_key, timestamp, NULL);
}

bool curl_event_plugin_pubsub_seek_to_snapshot_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    const char *token_state_key,
    const char *snapshot
) {
    return curl_event_plugin_pubsub_seek_init(loop, project_id, subscription_id, token_state_key, NULL, snapshot);
}
