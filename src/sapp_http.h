// Copyright (c) 2026 Jericho Crosby (Chalwk)
// Licensed under the MIT License.

#pragma once

#include <stddef.h>
#include <stdint.h>

// ------------------------------------------------------------------
//  Platform-specific export/import macros
// ------------------------------------------------------------------
#ifdef _WIN32
#ifdef SAPPHTTP_BUILD
#define SAPPHTTP_API __declspec(dllexport)
#else
#define SAPPHTTP_API __declspec(dllimport)
#endif
#define SAPPHTTP_CALL __cdecl
#else
#define SAPPHTTP_API __attribute__((visibility("default")))
#define SAPPHTTP_CALL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    // Return codes for the API - negative values indicate errors.
    enum sapp_http_status
    {
        SAPPHTTP_OK = 0,
        SAPPHTTP_E_INVALID_ARGUMENT = -1,
        SAPPHTTP_E_CURL_INIT_FAILED = -2,
        SAPPHTTP_E_CURL_OPTION_FAILED = -3,
        SAPPHTTP_E_OUT_OF_MEMORY = -4
    };

    // A single HTTP header.
    typedef struct sapp_http_header
    {
        const char *name;
        const char *value;
    } sapp_http_header;

    // Holds the full HTTP response.
    typedef struct sapp_http_response
    {
        int curl_code;
        long http_status;
        size_t body_size;
        char *body;
        char *content_type;
        char *error_message;
    } sapp_http_response;

    // Opaque handle for an asynchronous request.
    typedef struct sapp_http_request sapp_http_request;

    // ------------------------------------------------------------------
    //  Global initialisation / cleanup
    // ------------------------------------------------------------------

    SAPPHTTP_API int SAPPHTTP_CALL sapp_http_global_init(void);
    SAPPHTTP_API void SAPPHTTP_CALL sapp_http_global_cleanup(void);

    // ------------------------------------------------------------------
    //  Asynchronous (non‑blocking) API
    // ------------------------------------------------------------------

    // Create a new asynchronous GET request. Returns an opaque handle,
    // or NULL on failure. The request is added to the internal multi handle.
    SAPPHTTP_API sapp_http_request *SAPPHTTP_CALL sapp_http_create_get(
        const char *url,
        const sapp_http_header *headers,
        size_t header_count);

    // Create a new asynchronous POST request.
    SAPPHTTP_API sapp_http_request *SAPPHTTP_CALL sapp_http_create_post(
        const char *url,
        const char *content_type,
        const char *body,
        size_t body_size,
        const sapp_http_header *headers,
        size_t header_count);

    // Create a new asynchronous PUT request.
    SAPPHTTP_API sapp_http_request *SAPPHTTP_CALL sapp_http_create_put(
        const char *url,
        const char *content_type,
        const char *body,
        size_t body_size,
        const sapp_http_header *headers,
        size_t header_count);

    // Process all pending asynchronous requests. Call this regularly (e.g. from a Lua timer).
    // Returns the number of still-active requests, or a negative error code.
    SAPPHTTP_API int SAPPHTTP_CALL sapp_http_process(void);

    // Check if an asynchronous request has finished. Returns 1 if done, 0 if still in progress,
    // negative on error.
    SAPPHTTP_API int SAPPHTTP_CALL sapp_http_request_is_done(sapp_http_request *req);

    // Retrieve the response of a finished request. The data is copied into `out`.
    // You must call sapp_http_free_response() on `out` later.
    // Returns 0 on success, or an error code.
    SAPPHTTP_API int SAPPHTTP_CALL sapp_http_request_get_response(
        sapp_http_request *req,
        sapp_http_response *out);

    // Free all resources associated with an asynchronous request.
    // Safe to call even if the request is still active (it will be removed first).
    SAPPHTTP_API void SAPPHTTP_CALL sapp_http_request_free(sapp_http_request *req);

    // ------------------------------------------------------------------
    //  Utilities
    // ------------------------------------------------------------------

    SAPPHTTP_API void SAPPHTTP_CALL sapp_http_free_response(sapp_http_response *response);
    SAPPHTTP_API const char *SAPPHTTP_CALL sapp_http_version(void);
    SAPPHTTP_API const char *SAPPHTTP_CALL sapp_http_curl_strerror(int curl_code);

#ifdef __cplusplus
}
#endif