# SPDX-FileCopyrightText: 2025 Andy Curtis
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(openai_structured_examples C)

# Pulls in a-curl-library + helpers
set(CUSTOM_PACKAGES a-curl-library)

# One target per tiny example
set(BINARY_SOURCES
    src/structured_single_object.c
    src/structured_array.c
    src/structured_nested.c
    src/structured_chain_single_followup.c
    src/structured_chain_parallel_followup.c
    src/structured_validation_error.c
    src/structured_contact_form.c
    src/structured_calendar_event.c
    src/structured_invoice_line_items.c
    src/structured_moderation_report.c
    src/structured_ui_tree.c
    src/structured_search_query.c
    src/structured_options_with_null.c
    src/structured_streaming_entities.c
    src/structured_chain_generate_then_expand.c
    src/structured_validation_showcase.c
)

find_package(a-cmake-library REQUIRED)
include(BinaryConfig)            # auto-links & builds each file