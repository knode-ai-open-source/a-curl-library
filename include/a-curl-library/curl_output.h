// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#ifndef CURL_OUTPUT_H
#define CURL_OUTPUT_H

#include <curl/curl.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward‑declare the request structure */
struct curl_event_request_s;

/* Generic “output sink” used by default callbacks ------------------------ */
typedef struct curl_output_interface_s {
    bool   (*init)    (struct curl_output_interface_s *self, long content_len);
    size_t (*write)   (const void *data, size_t size, size_t nmemb,
                       struct curl_output_interface_s *self);
    void   (*failure) (CURLcode result, long http_code,
                       struct curl_output_interface_s *self,
                       struct curl_event_request_s *req);
    void   (*complete)(struct curl_output_interface_s *self,
                       struct curl_event_request_s *req);
    void   (*destroy) (struct curl_output_interface_s *self);
} curl_output_interface_t;

/* Helper that wires the default callbacks into a request */
void curl_output_defaults(struct curl_event_request_s *req,
                          curl_output_interface_t *output);
#endif /* CURL_OUTPUT_H */
