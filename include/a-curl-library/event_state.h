// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
#ifndef EVENT_STATE_H
#define EVENT_STATE_H

/* Minimal, self‑contained key‑value “state” API used by the loop
   (kept public so clients can query / set keys if desired). */

struct curl_event_loop_s;

/* Store or clear (`value == NULL`) a state key */
void  curl_event_loop_put_state(struct curl_event_loop_s *loop,
                                const char *key, const char *value);

/* Retrieve a *copy* of the state value (caller must free) */
char *curl_event_loop_get_state(struct curl_event_loop_s *loop,
                                const char *key);

#endif /* EVENT_STATE_H */
