// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef _curl_event_plugin_gcloud_token_H
#define _curl_event_plugin_gcloud_token_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Payload published under the single resource id. */
typedef struct gcloud_token_payload_s {
    char   *access_token;     /* NUL-terminated, owned by payload */
    bool    metadata_flavor;  /* true if token came from metadata server */
    time_t  expires_at;       /* absolute UNIX time when token expires */
} gcloud_token_payload_t;

/* Cleanup function used by curl_resource. */
void gcloud_token_payload_free(void *p);


curl_event_request_t *curl_event_plugin_gcloud_token_init(
    curl_event_loop_t *loop,
    const char *key_filename,
    curl_event_res_id token_id,
    bool should_refresh
);

#ifdef __cplusplus
}
#endif

#endif // _plugin_gcloud_token_H