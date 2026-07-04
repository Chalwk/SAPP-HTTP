// Copyright (c) 2026 Jericho Crosby (Chalwk)
// Licensed under the MIT License.

#pragma once

#include <stddef.h>
#include <stdint.h>

// ------------------------------------------------------------------
//  Platform-specific export/import macros
//  For Windows we use __declspec(dllexport/dllimport), 
//  for others we rely on default visibility.
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
  // SAPPHTTP_OK means success, but even then you should check the
  // `curl_code` inside the response for libcurl's own status.
  enum sapp_http_status
  {
    SAPPHTTP_OK = 0,
    SAPPHTTP_E_INVALID_ARGUMENT = -1,
    SAPPHTTP_E_CURL_INIT_FAILED = -2,
    SAPPHTTP_E_CURL_OPTION_FAILED = -3,
    SAPPHTTP_E_OUT_OF_MEMORY = -4
  };

  // A single HTTP header: name and value.
  // Both are plain C strings; the caller must keep them alive
  // while the request is being made.
  typedef struct sapp_http_header
  {
    const char *name;
    const char *value;
  } sapp_http_header;

  // Holds the full HTTP response.
  // - `curl_code` is the libcurl CURLcode (cast to int).
  // - `http_status` is the response status code (e.g. 200, 404).
  // - `body` and `body_size` contain the response body (if any).
  // - `content_type` is the Content-Type header from the server.
  // - `error_message` is set when something goes wrong (libcurl error or our wrapper error).
  //
  // All dynamically allocated strings inside are owned by the caller
  // and must be freed with sapp_http_free_response() after use.
  typedef struct sapp_http_response
  {
    int curl_code;
    long http_status;
    size_t body_size;
    char *body;
    char *content_type;
    char *error_message;
  } sapp_http_response;

  // Initialises libcurl globally. Call this once before making any requests.
  // Returns SAPPHTTP_OK on success, or an error code.
  SAPPHTTP_API int SAPPHTTP_CALL sapp_http_global_init(void);

  // Cleans up libcurl globally. Call this when you're done with all requests.
  SAPPHTTP_API void SAPPHTTP_CALL sapp_http_global_cleanup(void);

  // Perform a GET request.
  // `headers` is an array of `sapp_http_header`; `header_count` is its size.
  // The response is written into `out_response` - you must call sapp_http_free_response()
  // to free its dynamically allocated members.
  SAPPHTTP_API int SAPPHTTP_CALL sapp_http_get(
      const char *url,
      const sapp_http_header *headers,
      size_t header_count,
      sapp_http_response *out_response);

  // Perform a POST request.
  // `content_type` sets the Content-Type header (e.g. "application/json").
  // `body` and `body_size` are the payload to send.
  // If `body` is null, an empty body is sent.
  // Headers work the same as for GET.
  SAPPHTTP_API int SAPPHTTP_CALL sapp_http_post(
      const char *url,
      const char *content_type,
      const char *body,
      size_t body_size,
      const sapp_http_header *headers,
      size_t header_count,
      sapp_http_response *out_response);

  // Perform a PUT request - same parameters as POST.
  SAPPHTTP_API int SAPPHTTP_CALL sapp_http_put(
      const char *url,
      const char *content_type,
      const char *body,
      size_t body_size,
      const sapp_http_header *headers,
      size_t header_count,
      sapp_http_response *out_response);

  // Frees all internal buffers of a response structure.
  // After calling this, the response pointer itself is still valid
  // (it's usually on the stack), but its members are nulled/zeroed.
  SAPPHTTP_API void SAPPHTTP_CALL sapp_http_free_response(sapp_http_response *response);

  // Returns the libcurl version string - handy for debugging.
  SAPPHTTP_API const char *SAPPHTTP_CALL sapp_http_version(void);

  // Converts a libcurl CURLcode (as int) to a human-readable error string.
  SAPPHTTP_API const char *SAPPHTTP_CALL sapp_http_curl_strerror(int curl_code);

#ifdef __cplusplus
}
#endif