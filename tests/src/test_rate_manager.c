// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/rate_manager.h"
#include "the-macro-library/macro_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// Test function to simulate requests
void test_rate_manager() {
    const char *test_key = "api_test";
    int max_concurrent = 10;
    int max_rps = 5;

    printf("Setting rate limit for '%s' (max %d concurrent, %d requests per second)\n",
           test_key, max_concurrent, max_rps);
    rate_manager_set_limit(test_key, max_concurrent, max_rps);

    printf("\n--- Testing rate limits ---\n");

    // Try sending more requests than allowed per second
    int allowed = 0, denied = 0;
    uint64_t start_time = macro_now();

    for (int i = 0; i < 10; i++) {
        if (rate_manager_start_request(test_key, false)) {
            allowed++;
            printf("[✅] Request %d allowed\n", i + 1);
        } else {
            denied++;
            printf("[❌] Request %d denied (rate limit reached)\n", i + 1);
        }
        usleep(10000); // 100ms between requests
    }

    uint64_t end_time = macro_now();
    printf("\nAllowed requests: %d, Denied requests: %d\n", allowed, denied);
    printf("Elapsed time: %.3f seconds\n\n", macro_time_diff(start_time, end_time));

    // Simulate request completion
    printf("--- Marking requests as done ---\n");
    for (int i = 0; i < allowed; i++) {
        rate_manager_request_done(test_key);
        printf("[✔] Marked request %d as done\n", i + 1);
        usleep(50000); // 50ms delay between each completion
    }

    // Test handling of 429 Too Many Requests
    printf("\n--- Testing 429 Handling ---\n");
    for (int i = 0; i < 5; i++) {
        int backoff = rate_manager_handle_429(test_key);
        printf("[⚠️] 429 received. Backoff time: %d seconds\n", backoff);
        sleep(backoff);
    }

    printf("\nFinal backoff should not exceed 60 seconds...\n");
}

int main() {
    test_rate_manager();
    return 0;
}
