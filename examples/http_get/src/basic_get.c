// SPDX-FileCopyrightText: 2025 Andy Curtis
// SPDX-License-Identifier: Apache-2.0

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/outputs/memory.h"
#include <stdio.h>
#include <stdlib.h>

/* Called when the download finishes (success or failure). */
static void on_mem_done(
    char *data, size_t len, bool success,
    CURLcode result, long http, const char *err,
    void *arg, curl_event_request_t *req)
{
    (void)arg; (void)req;
    if (!success) {
        fprintf(stderr, "GET failed: curl=%d http=%ld err=%s\n",
                result, http, err ? err : "(none)");
        return;
    }
    fwrite(data, 1, len, stdout);
    fputc('\n', stdout);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <url>\n", argv[0]);
        return 1;
    }
    const char *url = argv[1];

    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);
    curl_output_interface_t *out = memory_output(on_mem_done, NULL);

    curl_event_request_t *r = curl_event_request_build_get(
        url,
        NULL,   // using output sink instead of a manual write_cb
        NULL,
        out     // pass sink as userdata
    );

    // Optional hardening
    curl_output_defaults(r, out);
    curl_event_request_apply_browser_profile(r, NULL, NULL);
    curl_event_request_connect_timeout(r, 10);   // seconds
    curl_event_request_transfer_timeout(r, 30);  // seconds
    curl_event_request_low_speed(r, 1000, 10);   // <1KB/s for 10s => abort

    curl_event_request_submit(loop, r, /*priority*/0);
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
    return 0;
}
