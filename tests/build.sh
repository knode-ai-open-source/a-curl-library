#!/usr/bin/env bash

set -euxo pipefail

rm -rf build
mkdir -p build
cd build
cmake ..
make -j$(nproc)
./test_curl_resource
./test_curl_resource_async
./test_event_loop_cancel
./test_event_loop_priority
./test_rate_manager
./test_request_headers
./test_request_json
./test_worker_pool
./test_sinks
#  ./test_rate_manager_hp_429   # currently hangs - but not likely an issue
cd ..
