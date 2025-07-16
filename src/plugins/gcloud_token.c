// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
// Maintainer: Andy Curtis <contactandyc@gmail.com>
#include "a-curl-library/plugins/gcloud_token.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-json-library/ajson.h"
#include "the-io-library/io.h"
#include "a-memory-library/aml_buffer.h"
#include "the-macro-library/macro_time.h"

#include <jwt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pwd.h>


/* =======================
   Internal struct
   ======================= */
typedef struct curl_event_plugin_gcloud_token_s {
    curl_event_loop_t *loop;

    // for service account
    char *private_key;    // RSA private key in PEM format
    char *client_email;   // Service account email

    // for authorized user
    char *client_id;
    char *client_secret;
    char *refresh_token;

    char *token_state_key;

    bool metadata_flavor;

    // Bookkeeping for how many times we've refreshed the token
    int  token_refreshes;

    // Token expiration time (UNIX timestamp)
    time_t expires_at;


    aml_buffer_t *response_bh;
} curl_event_plugin_gcloud_token_t;

/* =======================
   Constants
   ======================= */
static const char *GOOGLE_OAUTH_TOKEN_URL = "https://oauth2.googleapis.com/token";
static const char *GOOGLE_METADATA_TOKEN_URL = "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token";

static char *find_key_file(const char *filename) {
    char cwd[PATH_MAX];
    char path[PATH_MAX];

    // Attempt to find the key file in the current directory or its parents
    if (getcwd(cwd, sizeof(cwd))) {
        while (1) {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
            snprintf(path, sizeof(path)-2, "%s/%s", cwd, filename);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
            if (access(path, F_OK) == 0) {
                // File found
                return aml_strdup(path);
            }

            // Move to the parent directory
            char *last_slash = strrchr(cwd, '/');
            if (!last_slash) break; // Reached root directory
            *last_slash = '\0';
        }
    } else {
        fprintf(stderr, "Failed to get current working directory.\n");
    }

    // If not found, check the default location for application credentials
    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home_dir = pw->pw_dir;
    }

    if (home_dir) {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
        snprintf(path, sizeof(path)-100, "%s/.config/gcloud/application_default_credentials.json", home_dir);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        if (access(path, F_OK) == 0) {
            // File found in the default location
            return aml_strdup(path);
        }
    }

    // File not found
    return NULL;
}

// Parse the service account JSON key file using `ajson`.
static bool parse_service_account_key(const char *key_file, curl_event_plugin_gcloud_token_t *h) {
    aml_pool_t *pool = aml_pool_init(2048);
    size_t length = 0;
    char *key_data = io_pool_read_file(pool, &length, key_file);
    if (!key_data) {
        fprintf(stderr, "Failed to read key file: %s\n", key_file);
        aml_pool_destroy(pool);
        return false;
    }

    ajson_t *json = ajson_parse_string(pool, key_data);
    if (ajson_is_error(json)) {
        fprintf(stderr, "Failed to parse account key file.\n");
        aml_pool_destroy(pool);
        return false;
    }

    char *type = ajsono_scan_strd(pool, json, "type", NULL);
    if(!type) {
        fprintf(stderr, "Malformed account key file.\n");
        aml_pool_destroy(pool);
        return false;
    }
    if(!strcmp(type, "service_account")) {
        char *client_email = ajsono_scan_strd(pool, json, "client_email", NULL);
        char *private_key = ajsono_scan_strd(pool, json, "private_key", NULL);
        if(!client_email || !private_key) {
            fprintf(stderr, "Malformed service account key file.\n");
            aml_pool_destroy(pool);
            return false;
        }
        h->client_email = aml_strdup(client_email);
        h->private_key = aml_strdup(private_key);
        fprintf( stderr, "Using service account credentials.\n");
    } else if(!strcmp(type, "authorized_user")) {
        char *client_id = ajsono_scan_strd(pool, json, "client_id", NULL);
        char *client_secret = ajsono_scan_strd(pool, json, "client_secret", NULL);
        char *refresh_token = ajsono_scan_strd(pool, json, "refresh_token", NULL);
        h->client_id = aml_strdup(client_id);
        h->client_secret = aml_strdup(client_secret);
        h->refresh_token = aml_strdup(refresh_token);
        fprintf( stderr, "Using authorized user credentials.\n");
    } else {
        fprintf(stderr, "Unknown key file type: %s\n", type);
        aml_pool_destroy(pool);
        return false;
    }

    aml_pool_destroy(pool);
    return true;
}

static jwk_item_t *make_rsa_jwk(const char *pem, jwt_alg_t alg)
{
    /* Build a minimal JWK in JSON */
    char jwk_json[4096];
    snprintf(jwk_json, sizeof(jwk_json),
             "{ \"kty\":\"RSA\", \"use\":\"sig\", \"alg\":\"%s\", \"pem\":\"%s\" }",
             jwt_alg_str(alg), pem);

    jwk_set_t *set = jwks_load(NULL, jwk_json);      /* create one-item set */
    if (!set || jwks_error(set))
        return NULL;

    return (jwk_item_t *)jwks_item_get(set, 0);      /* valid while set lives */
}

/* ────────────────────────────
   Replacement for the old grant-based generate_jwt()
   ──────────────────────────── */
static char *generate_jwt(const curl_event_plugin_gcloud_token_t *h)
{
    jwt_builder_t *b = jwt_builder_new();            /* create builder        */
    if (!b) {
        perror("jwt_builder_new");
        return NULL;
    }

    /* attach signing key (PEM → JWK → builder) */
    jwk_item_t *key = make_rsa_jwk(h->private_key, JWT_ALG_RS256);
    if (!key || jwt_builder_setkey(b, JWT_ALG_RS256, key) != 0) {
        fprintf(stderr, "jwt_setkey: %s\n", jwt_builder_error_msg(b));
        jwt_builder_free(b);
        return NULL;
    }

    time_t now = time(NULL);
    jwt_value_t v;          /* reusable claim container */

    jwt_set_SET_STR(&v, "iss",   h->client_email);                         jwt_builder_claim_set(b, &v);
    jwt_set_SET_STR(&v, "scope", "https://www.googleapis.com/auth/cloud-platform"); jwt_builder_claim_set(b, &v);
    jwt_set_SET_STR(&v, "aud",   "https://oauth2.googleapis.com/token");   jwt_builder_claim_set(b, &v);
    jwt_set_SET_INT(&v, "iat",   now);                                     jwt_builder_claim_set(b, &v);
    jwt_set_SET_INT(&v, "exp",   now + 3600);                              jwt_builder_claim_set(b, &v);

    /* produce compact JWT string */
    char *token = jwt_builder_generate(b);         /* malloc-ed – caller frees */
    if (!token)
        fprintf(stderr, "jwt_builder_generate: %s\n", jwt_builder_error_msg(b));

    jwt_builder_free(b);                           /* frees key via jwks      */
    return token;
}

/* =======================
   cURL write callback
   ======================= */
static size_t token_write_cb(void *contents, size_t size, size_t nmemb, curl_event_request_t *req)
{
    // Our plugin struct; it contains the buffer fields
    struct curl_event_plugin_gcloud_token_s *ctx =
        (struct curl_event_plugin_gcloud_token_s *)req->userdata;

    // How many bytes were just received
    size_t total = size * nmemb;

    // If there's no content, just return
    if (total == 0) {
        return 0;
    }

    aml_buffer_append(ctx->response_bh, contents, total);
    return total;
}

/* =======================
   on_prepare callback
   =======================
   Build the JWT, then set the POST body:
   "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=<jwt>"
*/
static bool gcloud_on_prepare(curl_event_request_t *req)
{
    // Our integration object is in `req->userdata`
    struct curl_event_plugin_gcloud_token_s *gct = (struct curl_event_plugin_gcloud_token_s *)req->userdata;

    if(gct->private_key && gct->client_email) {

        // Generate JWT
        char *jwt = generate_jwt(gct);
        if (!jwt) {
            fprintf(stderr, "[gcloud_on_prepare] Failed to generate JWT.\n");
            return false; // cURL won't proceed
        }

        // Build the post data
        static const char *fmt = "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=%s";
        size_t needed = strlen(fmt) + strlen(jwt) + 1;
        char *post_data = (char *)aml_zalloc(needed);
        sprintf(post_data, fmt, jwt);

        // Attach to the request
        aml_free(req->post_data); // if previously set
        req->post_data = post_data;

        // Freed local resources
        free(jwt);
    } else if(gct->client_id && gct->client_secret && gct->refresh_token) {
       char fmt[] = "grant_type=refresh_token&client_id=%s&client_secret=%s&refresh_token=%s";
       size_t needed = strlen(fmt) + strlen(gct->client_id)
                       + strlen(gct->client_secret) + strlen(gct->refresh_token) + 1;
       char *post_data = aml_zalloc(needed);
       snprintf(post_data, needed, fmt, gct->client_id, gct->client_secret, gct->refresh_token);

       aml_free(req->post_data);
       req->post_data = post_data;
    } else {
        fprintf(stderr, "[gcloud_on_prepare] Invalid credentials.\n");
        return false;
    }

    // Force method to POST
    if(req->method && strcmp(req->method, "POST") != 0) {
        aml_free(req->method);
        req->method = NULL;
    }
    if(!req->method)
        req->method = aml_strdup("POST");

    return true;
}

static int gcloud_on_failure(
    CURL *easy_handle,
    CURLcode result,
    long http_code,
    curl_event_request_t *req
) {
    // is there cases where the failure is not transient?
    // retry forever (every 2 seconds)
    return 2;
}

/* =======================
   on_complete callback
   =======================
   Parse the JSON, store `access_token` in loop->state, get `expires_in`,
   then schedule next refresh in (expires_in - 60) seconds or similar.
*/
static int gcloud_on_complete(
    CURL *easy_handle,
    curl_event_request_t *req
) {
    (void)easy_handle; // unused here

    // Our integration object
    struct curl_event_plugin_gcloud_token_s *gct = (struct curl_event_plugin_gcloud_token_s *)req->userdata;

    gct->expires_at = time(NULL);
    req->next_retry_at = 0;

    aml_pool_t *pool = aml_pool_init(1024);
    ajson_t *json_response = ajson_parse_string(pool, aml_buffer_data(gct->response_bh));

    if (ajson_is_error(json_response)) {
        fprintf(stderr, "Failed to parse token response.\n");
        aml_pool_destroy(pool);
        return 2; // retry in two seconds
    }
    char *access_token = ajson_extract_string(pool, ajsono_scan(json_response, "access_token"));
    int expires_in = ajsono_scan_int(json_response, "expires_in", 0);
    if(!access_token || !expires_in) {
        fprintf(stderr, "Malformed token response.\n");
        aml_pool_destroy(pool);
        return 2;
    }

    // we'll refresh 360s before it actually expires
    int lead_seconds = 360;
    if (expires_in < (lead_seconds + 1)) {
        expires_in /= 2; // retry in half the time
    }

    int next_refresh = expires_in - lead_seconds;
    gct->token_refreshes++;
    gct->expires_at = macro_now_add_seconds(next_refresh);

    // Store the token in the loop state
    curl_event_loop_put_state(req->loop,
                              aml_pool_strdupf(pool, "%s_metadata_flavor", gct->token_state_key),
                              gct->metadata_flavor ? "true" : "false");
    curl_event_loop_put_state(req->loop, gct->token_state_key, access_token);
    aml_pool_destroy(pool);
    fprintf( stderr, "Received access token: %.10s..., expires in %d second(s), retrying in %d second(s)\n",
    		 access_token, expires_in, next_refresh);
    return next_refresh;
}

/* =======================
   Integration destroy
   ======================= */
static void gcloud_token_destroy(curl_event_plugin_gcloud_token_t *gct)
{
    if (!gct) return;
    if(gct->private_key)
        aml_free(gct->private_key);
    if(gct->client_email)
        aml_free(gct->client_email);
    if(gct->client_id)
        aml_free(gct->client_id);
    if(gct->client_secret)
        aml_free(gct->client_secret);
    if(gct->refresh_token)
        aml_free(gct->refresh_token);

    aml_buffer_destroy(gct->response_bh);
    aml_free(gct->token_state_key);
    aml_free(gct);
}


/* =======================
   Integration init
   ======================= */
curl_event_request_t *curl_event_plugin_gcloud_token_init(curl_event_loop_t *loop,
                                                          const char *key_filename,
                                                          const char *token_state_key,
                                                          bool should_refresh) {
    if (!loop || !key_filename || !token_state_key) {
        fprintf(stderr, "[gcloud_token_init] invalid arguments.\n");
        return NULL;
    }

    // Find the key file
    char *key_file = find_key_file(key_filename);

    // Allocate the integration struct
    curl_event_plugin_gcloud_token_t *gct =
        (curl_event_plugin_gcloud_token_t *)aml_zalloc(sizeof(*gct));
    if (!gct) {
        fprintf(stderr, "[gcloud_token_init] Out of memory.\n");
        return NULL;
    }
    if(key_file) {
        if(!parse_service_account_key(key_file, gct)) {
            fprintf(stderr, "[gcloud_token_init] Failed to parse service account key.\n");
            aml_free(gct);
            return NULL;
        }
        aml_free(key_file);
    }
    gct->token_state_key  = aml_strdup(token_state_key);
    gct->token_refreshes  = 0;
    gct->response_bh = aml_buffer_init(1024);
    gct->metadata_flavor = false;

    // Build the request object
    curl_event_request_t req;
    memset(&req, 0, sizeof(req));
    req.loop         = loop;

    struct curl_slist *headers = NULL;
    if(!gct->client_email && !gct->client_id) {
        headers = curl_slist_append(headers, "Metadata-Flavor: Google");
        req.url          = (char *)GOOGLE_METADATA_TOKEN_URL;
        gct->metadata_flavor = true;
    } else {
        req.url          = (char *)GOOGLE_OAUTH_TOKEN_URL;
        req.on_prepare   = gcloud_on_prepare;
    }
    req.headers      = headers;
    req.on_complete  = gcloud_on_complete;
    req.on_failure   = gcloud_on_failure;
    req.should_refresh   = should_refresh;
    req.max_retries  = 10;
    req.connect_timeout = 10;
    req.transfer_timeout = 30;

    req.userdata_cleanup = (curl_event_cleanup_userdata_t)gcloud_token_destroy;
    req.userdata     = gct;
    req.write_cb     = token_write_cb;   // store the response in `response_bh`

    // Priority = 0 => start now
    curl_event_request_t *r = curl_event_loop_enqueue(loop, &req, 0);
    if(headers)
        curl_slist_free_all(headers);

    if (!r) {
        fprintf(stderr, "[gcloud_token_init] Failed to enqueue token request.\n");
        // cleanup
        gcloud_token_destroy(gct);
        return NULL;
    }

    return r;
}
