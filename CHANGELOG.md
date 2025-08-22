## 08/22/2025

**Core API & Packaging Overhaul — request builders, resource dependencies, “sinks”, multi-variant CMake, Docker, examples**

## Summary

* Rewrote the public API around **`curl_event_request_*`** builders + **`curl_event_loop_submit`** (no more “enqueue and copy”).
* Introduced a lightweight **resource dependency system** (`curl_resource.*`) to replace the old loop “state” map.
* Replaced “outputs/” with **“sinks/”** (e.g. `memory_sink`, `file_sink`) and made callbacks simpler.
* Modernized CMake: **multi-variant** targets (debug, memory, static, shared), exported config, coverage toggle, **`build.sh`** helper.
* Added **Dockerfile** (Ubuntu-based) and **HTTP GET examples**.
* Removed third-party plugins (OpenAI, Google, Pub/Sub, CloudSQL, Spanner, Vision) from the core.
* Tidied repo: updated AUTHORS/NOTICE, .gitignore, and overhauled BUILDING.md.

---

## Why

The core library had grown to include product-specific integrations and an implicit “state” system. This refactor narrows the surface area to a clean, reusable CURL event loop + request model, while making it easier to package, test, and depend on from other projects. Plugins can live out-of-tree.

---

## What changed (high level)

### 1) Core API (new headers)

* **New**: `include/a-curl-library/curl_event_request.h`
  Request struct + builder/mutator functions, retry/backoff, per-request HTTP/3 toggle, JSON helpers (object/array root + auto `Content-Type`), and an ergonomic **submit** API.
* **New**: `include/a-curl-library/curl_resource.h`
  Minimal **resource DAG**: declare/register a resource, publish/republish values (sync/async), and let requests **depend** on resource IDs. The loop blocks until dependencies are ready (no more string-key state).
* **Updated**: `include/a-curl-library/curl_event_loop.h`
  Now focuses on loop lifecycle + metrics + **`curl_event_loop_submit()`** / cancel. The loop no longer copies requests; you build a request object and submit **that exact instance**.
* **Renamed** outputs → **sinks**:

    * `include/a-curl-library/sinks/memory.h` (was `outputs/memory.h`)
    * `include/a-curl-library/sinks/file.h`   (was `outputs/file.h`)
      Sinks attach to a request and provide `(init|write|complete|failure|destroy)`.
* **Polish**: `include/a-curl-library/rate_manager.h` prototypes updated (`void` suffixes) and header SPDX/authors normalized.

### 2) Removed from the core

* All product-specific *plugins* & *outputs*: OpenAI (chat/embed), Google (Custom Search, Vision, Embeddings), Pub/Sub, Cloud SQL, Spanner.
  Rationale: keep the core lean; these can be layered on top using the new APIs (sinks + resources) in downstream repos.

### 3) Build & packaging

* **CMake 3.20+**, project renamed for CMake hygiene:

    * Project: `a_curl_library` (C), **version 0.0.1**.
    * Builds **all variants**: `a_curl_library_{debug|memory|static|shared}`.
    * Umbrella alias **`a_curl_library::a_curl_library`** points to one variant chosen by `-DA_BUILD_VARIANT=...`.
    * Exports **Config/Version/Targets** to `cmake/a_curl_library` so consumers can `find_package(a_curl_library CONFIG REQUIRED)`.
    * **Coverage**: `-DA_ENABLE_COVERAGE=ON` (Clang/GCC).
* **`build.sh`**: one-shot `build`/`install` + `coverage` + `clean`.
* **Dockerfile**: Ubuntu base, installs toolchain + CMake, builds deps (`a-memory-library`, `the-macro-library`, `a-json-library`), then this project.
* **BUILDING.md**: now shows local build, apt deps, optional dev tools, and Docker path.
* **.gitignore**: add `build-*` dirs, coverage outputs.
* **Changelog tooling removed**: delete `.changes/*`, `.changie.yaml`, `CHANGELOG.md`.

### 4) Examples

* `examples/http_get/`

    * `basic_get`: fetch a single URL to stdout via `memory_sink`.
    * `fetch_list`: read URLs from `data/popular_urls.txt`, fetch concurrently, print results.
    * Standalone CMake; shows how to consume `a_curl_library::a_curl_library`.

---

## Breaking changes & migration guide

### CMake consumers

**Before**

```cmake
find_package(a-cmake-library REQUIRED) # and other internal helpers
# target names not standardized
```

**After**

```cmake
find_package(a_curl_library CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE a_curl_library::a_curl_library)
# Choose variant at configure time (for *consumers* and examples):
#   -DA_BUILD_VARIANT=debug|memory|static|shared   (default: debug)
```

### Headers & names

* `outputs/` → **`sinks/`**:

    * `#include "a-curl-library/sinks/memory.h"`
    * `#include "a-curl-library/sinks/file.h"`
* New request APIs:

    * `#include "a-curl-library/curl_event_request.h"`
    * `#include "a-curl-library/curl_resource.h"`

### Enqueue → Submit (and no implicit copy)

**Before**

```c
curl_event_request_t req = {0};
req.url = "...";
req.on_complete = ...;
req.write_cb = ...; // often through an “output” wrapper
curl_event_loop_enqueue(loop, &req, /*priority*/0); // loop copied fields
```

**After**

```c
curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

curl_event_request_t *r =
  curl_event_request_build_get("https://example.com/",
                               /*write_cb*/NULL,
                               /*on_complete*/NULL);

/* Attach a memory sink to capture body and notify on completion */
memory_sink(r, on_mem_done, /*cb_arg*/NULL);

/* Optional ergonomics */
curl_event_request_apply_browser_profile(r, NULL, NULL);
curl_event_request_connect_timeout(r, 10);
curl_event_request_transfer_timeout(r, 30);
curl_event_request_low_speed(r, 1000, 10);

/* Submit the *same* object you built */
curl_event_request_submit(loop, r, /*priority*/0);
curl_event_loop_run(loop);
```

### Loop “state” → Resource dependencies

**Before**

```c
curl_event_loop_put_state(loop, "OAUTH", token);
req.dependencies = (char*[]){"OAUTH", NULL};
```

**After**

```c
/* Declare/register a resource once (or keep its ID around globally) */
curl_event_res_id OAUTH = curl_event_res_register(loop, strdup(token), free);

/* Make requests depend on it */
curl_event_request_depend(r, OAUTH);

/* Later, rotate token (from any thread): */
curl_event_res_publish_async(loop, OAUTH, strdup(new_token), free);
```

### Outputs → Sinks

**Before**

```c
curl_output_interface_t *out = memory_output(on_done, arg);
curl_output_defaults(&req, out);
```

**After**

```c
memory_sink(r, on_done, arg);     // attaches sink to request
/* or */
file_sink(r, "/tmp/out.bin", on_file_done, arg);
```

### Header updates

**Before**: `curl_event_loop_update_header(req, "Authorization", "Bearer ...");`
**After** : `curl_event_request_set_header(req, "Authorization", "Bearer ...");`

### Other notes

* `rate_manager_init()` / `rate_manager_destroy()` now use explicit `void` prototypes.
* Project version is `0.0.1` (reset for the re-scoped core).

---

## Build / Install quickstart

**Local**

```bash
./build.sh install             # builds and installs to /usr/local
# or:
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc)"
sudo cmake --install .
```

**Deps (Ubuntu)**

```bash
sudo apt-get update && sudo apt-get install -y \
  libssl-dev libcurl4-openssl-dev zlib1g-dev build-essential \
  valgrind gdb python3 python3-venv python3-pip perl autoconf automake libtool
```

**Docker**

```bash
docker build -t a-curl-library:dev .
docker run --rm -it a-curl-library:dev
```

---

## Testing Plan

* **Unit/ctest**
  From a normal build: `ctest --output-on-failure`
* **Examples**

  ```bash
  cd examples/http_get && ./build.sh
  ./build/basic_get https://example.com/
  ./build/fetch_list ./data/popular_urls.txt
  ```
* **Coverage (Clang/GCC)**

  ```bash
  ./build.sh coverage
  # HTML report at build-coverage/tests/coverage_html/index.html
  ```

---

## Risks & mitigations

* **API/ABI break**: core names and headers changed; plugins removed.
  → Documented migration above; examples included; packaging via `find_package`.
* **Version reset**: now `0.0.1` to reflect the narrowed core scope.
  → Treat as a fresh series; tags/releases can communicate the “rewrite” milestone.
* **Downstream plugins**: removed from core.
  → Plan is to re-publish as separate modules built on top of this API (sinks + resources).

---

## Developer notes / nice touches

* **Faster dev loops**: no per-enqueue copies; request objects live in a small per-request pool.
* **Backoff**: defaults to **full jitter** with min/max clamp knobs.
* **HTTP/3**: enable globally on the loop or per request override.
* **Max download size**: enforced via Content-Length and during body writes.

---

## Follow-ups (separate PRs welcome)

* Publish plugin modules (OpenAI, Google, Pub/Sub, etc.) out-of-tree using sinks/resources.
* Additional examples: POST JSON, file downloads, HTTP/3 on/off demo.
* Docs: short guide for the **resource API** patterns and JSON builder helpers.

---

## Checklist

* [x] Builds locally (`build.sh`) and via CMake/Ninja/Unix Makefiles
* [x] Installs exported CMake package config
* [x] Examples compile and run
* [x] Docker image builds
* [x] SPDX/authors/notice updated
* [x] Old changelog tooling removed
* [x] Breaking changes & migration guide documented

---

**Screenshots / Logs**
N/A — see `examples/http_get` output for quick manual verification.
