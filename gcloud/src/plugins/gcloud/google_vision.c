// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcloud/google_vision.h"
#include "a-curl-library/rate_manager.h"
#include "a-curl-library/curl_resource.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Base URL for Google Vision API (without API key) */
static const char *GOOGLE_VISION_BASE_URL =
    "https://vision.googleapis.com/v1/images:annotate";

void curl_event_plugin_google_vision_set_rate(void) {
    /* name, bucket, refill/sec (tune as you need) */
    rate_manager_set_limit("google_vision", 5, 10.0);
}

/* Build JSON body once, keep it owned by the request. */
static char *build_web_detection_body(const char *image_url) {
    aml_pool_t *pool = aml_pool_init(16 * 1024);
    if (!pool) return NULL;

    ajson_t *root = ajsono(pool);
    ajson_t *requests = ajsona(pool);
    ajson_t *req = ajsono(pool);

    /* image.source.imageUri */
    ajson_t *image = ajsono(pool);
    ajson_t *source = ajsono(pool);
    ajsono_append(source, "imageUri", ajson_encode_str(pool, image_url), false);
    ajsono_append(image, "source", source, false);
    ajsono_append(req, "image", image, false);

    /* features: [{type: "WEB_DETECTION"}] */
    ajson_t *features = ajsona(pool);
    ajson_t *feature = ajsono(pool);
    ajsono_append(feature, "type", ajson_encode_str(pool, "WEB_DETECTION"), false);
    ajsona_append(features, feature);
    ajsono_append(req, "features", features, false);

    ajsona_append(requests, req);
    ajsono_append(root, "requests", requests, false);

    const char *tmp = ajson_stringify(pool, root);
    char *out = tmp ? aml_strdup(tmp) : NULL;

    aml_pool_destroy(pool);
    return out;
}

/**
 * Prepare: read API key string from the resource and append it to URL.
 * Also set Content-Type.
 */
static bool google_vision_on_prepare(curl_event_request_t *req) {
    /* We declared a dependency on the API key resource id at init time,
       so the payload is ready here (loop thread). */
    const char *api_key = (const char *)curl_event_res_peek(req->loop, req->dep_head->id);
    if (!api_key || !*api_key) {
        fprintf(stderr, "[google_vision] Missing API key.\n");
        return false;
    }

    /* Rebuild URL with ?key=… */
    char *old = req->url;
    char *neu = aml_strdupf("%s?key=%s", old, api_key);
    if (!neu) return false;
    aml_free(old);
    req->url = neu;

    /* Content-Type is required for JSON body */
    curl_event_loop_update_header(req, "Content-Type", "application/json");
    return true;
}

curl_event_request_t *curl_event_plugin_google_vision_init(
    curl_event_loop_t *loop,
    curl_event_res_id  api_key_id,
    const char *image_url,
    curl_output_interface_t *output_interface
) {
    if (!loop || api_key_id == 0 || !image_url || !output_interface) {
        fprintf(stderr, "[google_vision_init] Invalid arguments.\n");
        return NULL;
    }

    /* URL and body must live for the lifetime of the request. */
    char *url = aml_strdup(GOOGLE_VISION_BASE_URL);
    if (!url) {
        fprintf(stderr, "[google_vision_init] OOM for URL.\n");
        return NULL;
    }
    char *body = build_web_detection_body(image_url);
    if (!body) {
        aml_free(url);
        fprintf(stderr, "[google_vision_init] OOM building JSON body.\n");
        return NULL;
    }

    /* Build request */
    curl_event_request_t req = (curl_event_request_t){0};
    req.loop       = loop;
    req.url        = url;
    req.method     = "POST";
    req.post_data  = body;

    req.on_prepare = google_vision_on_prepare;
    req.rate_limit = "google_vision";
    curl_output_defaults(&req, output_interface);

    req.low_speed_limit = 1024; /* 1 KB/s */
    req.low_speed_time  = 15;
    req.max_retries     = 3;

    /* Depend on the API key string resource. */
    curl_event_request_depend(&req, api_key_id);

    curl_event_request_t *r = curl_event_loop_enqueue(loop, &req, 0);
    if (!r) {
        aml_free(url);
        aml_free(body);
        fprintf(stderr, "[google_vision_init] Failed to enqueue request.\n");
        return NULL;
    }
    return r;
}
