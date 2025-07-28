// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/impl/curl_event_priv.h"
#include "a-curl-library/curl_output.h"
#include "a-curl-library/curl_event_request.h"
#include <curl/curl.h>

static size_t default_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
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

static int default_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle; // Unused

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
    return 0; // Request succeeded
}

static int default_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle; // Unused

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

    return 0; // Failure is not transient
}

static void default_output_destroy(void *userdata) {
    curl_output_interface_t *output = (curl_output_interface_t *)userdata;
    if (output && output->destroy) {
        output->destroy(output);
    }
}

void curl_output_defaults(curl_event_request_t *req, curl_output_interface_t *output) {
    req->write_cb = default_on_write;
    req->on_complete = default_on_complete;
    req->on_failure = default_on_failure;
    req->userdata_cleanup = default_output_destroy;
    req->userdata = output;
}
