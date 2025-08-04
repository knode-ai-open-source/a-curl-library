# SPDX-FileCopyrightText: 2025 Andy Curtis
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(curl_examples C)

# Tell the helper we need a-curl-library
set(CUSTOM_PACKAGES a-curl-library)

# One or more executables (the helper auto-creates targets)
set(BINARY_SOURCES
    src/basic_get.c
    src/fetch_list.c
)

# Optional build-style toggles (pick one; DEBUG is on by default)
# set(STATIC_BUILD ON)
# set(SHARED_BUILD ON)
# set(DEBUG_BUILD  ON)
# set(ENABLE_CODE_COVERAGE ON)

find_package(a-cmake-library REQUIRED)
include(BinaryConfig)  # finds packages, creates exes, links them for you