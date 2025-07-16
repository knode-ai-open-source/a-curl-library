// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/worker_pool.h"
#include <stdio.h>
#include <unistd.h> // for sleep
#include <stdlib.h>

static void my_task(void *arg) {
    int *num = (int *)arg;
    printf("Thread got task: %d\n", *num);
    // Do some work...
}

int main(void) {
    // Create a pool of 4 threads
    worker_pool_t *pool = worker_pool_init(4);

    // Enqueue a few tasks
    for (int i = 0; i < 10; i++) {
        int *num = malloc(sizeof(int));
        *num = i;
        worker_pool_push(pool, my_task, num);
    }

    // Wait a bit (simulate main doing something else)
    sleep(2);

    // Stop and destroy the pool
    worker_pool_destroy(pool);

    return 0;
}
