#pragma once

#include <stddef.h>
#include <stdint.h>

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

  enum sapp_http_status
  {
    SAPPHTTP_OK = 0,
    SAPPHTTP_E_INVALID_ARGUMENT = -1,
    SAPPHTTP_E_CURL_INIT_FAILED = -2,
    SAPPHTTP_E_CURL_OPTION_FAILED = -3,
    SAPPHTTP_E_OUT_OF_MEMORY = -4
  };

  typedef struct sapp_http_header
  {
    const char *name;
    const char *value;
  } sapp_http_header;

  typedef struct sapp_http_response
  {
    int curl_code;
    long http_status;
    size_t body_size;
    char *body;
    char *content_type;
    char *error_message;
  } sapp_http_response;

  SAPPHTTP_API int SAPPHTTP_CALL sapp_http_global_init(void);
  SAPPHTTP_API void SAPPHTTP_CALL sapp_http_global_cleanup(void);

  SAPPHTTP_API int SAPPHTTP_CALL sapp_http_get(
      const char *url,
      const sapp_http_header *headers,
      size_t header_count,
      sapp_http_response *out_response);

  SAPPHTTP_API int SAPPHTTP_CALL sapp_http_post(
      const char *url,
      const char *content_type,
      const char *body,
      size_t body_size,
      const sapp_http_header *headers,
      size_t header_count,
      sapp_http_response *out_response);

  SAPPHTTP_API void SAPPHTTP_CALL sapp_http_free_response(sapp_http_response *response);

  SAPPHTTP_API const char *SAPPHTTP_CALL sapp_http_version(void);
  SAPPHTTP_API const char *SAPPHTTP_CALL sapp_http_curl_strerror(int curl_code);

#ifdef __cplusplus
}
#endif