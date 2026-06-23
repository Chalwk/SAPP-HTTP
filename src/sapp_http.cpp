#define _CRT_SECURE_NO_WARNINGS

#include "sapp_http.h"
#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <cctype>

namespace
{

    struct memory_buffer
    {
        char *data;
        size_t size;
    };

    std::mutex g_init_mutex;
    bool g_initialized = false;

    static char *dup_cstr(const char *s)
    {
        if (!s)
            return nullptr;
        const size_t len = std::strlen(s) + 1;
        char *out = static_cast<char *>(std::malloc(len));
        if (!out)
            return nullptr;
        std::memcpy(out, s, len);
        return out;
    }

    static void clear_response(sapp_http_response *r)
    {
        if (!r)
            return;
        r->curl_code = SAPPHTTP_OK;
        r->http_status = 0;
        r->body_size = 0;
        r->body = nullptr;
        r->content_type = nullptr;
        r->error_message = nullptr;
    }

    static void free_response_members(sapp_http_response *r)
    {
        if (!r)
            return;
        std::free(r->body);
        std::free(r->content_type);
        std::free(r->error_message);
        r->body = nullptr;
        r->content_type = nullptr;
        r->error_message = nullptr;
        r->body_size = 0;
        r->http_status = 0;
        r->curl_code = SAPPHTTP_OK;
    }

    static int set_wrapper_error(sapp_http_response *out, int code, const char *message)
    {
        if (out)
        {
            out->curl_code = code;
            out->http_status = 0;
            out->body_size = 0;
            out->body = nullptr;
            out->content_type = nullptr;
            out->error_message = dup_cstr(message);
        }
        return code;
    }

    static bool iequals(const char *a, const char *b)
    {
        if (!a || !b)
            return false;
#ifdef _WIN32
        return _stricmp(a, b) == 0;
#else
        return strcasecmp(a, b) == 0;
#endif
    }

    static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        const size_t total = size * nmemb;
        auto *buf = static_cast<memory_buffer *>(userdata);
        if (!buf || total == 0)
            return 0;

        const size_t needed = buf->size + total + 1;
        if (needed < buf->size || needed < total || needed > std::numeric_limits<size_t>::max())
            return 0;

        char *grown = static_cast<char *>(std::realloc(buf->data, needed));
        if (!grown)
            return 0;

        buf->data = grown;
        std::memcpy(buf->data + buf->size, ptr, total);
        buf->size += total;
        buf->data[buf->size] = '\0';
        return total;
    }

    static curl_slist *build_headers(const sapp_http_header *headers, size_t header_count, const char *content_type)
    {
        curl_slist *list = nullptr;
        if (content_type && *content_type)
        {
            std::string line = "Content-Type: ";
            line += content_type;
            curl_slist *next = curl_slist_append(list, line.c_str());
            if (!next)
            {
                curl_slist_free_all(list);
                return nullptr;
            }
            list = next;
        }
        for (size_t i = 0; i < header_count; ++i)
        {
            const char *name = headers[i].name;
            const char *value = headers[i].value;
            if (!name || !*name || !value)
                continue;
            std::string line = name;
            line += ": ";
            line += value;
            curl_slist *next = curl_slist_append(list, line.c_str());
            if (!next)
            {
                curl_slist_free_all(list);
                return nullptr;
            }
            list = next;
        }
        return list;
    }

    static int ensure_initialized_locked()
    {
        if (g_initialized)
            return SAPPHTTP_OK;
        const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (code != CURLE_OK)
            return SAPPHTTP_E_CURL_INIT_FAILED;
        g_initialized = true;
        return SAPPHTTP_OK;
    }

    static std::string resolve_via_doh(const std::string &host)
    {
        const char *url_google = "https://8.8.8.8/resolve?name=";
        const char *url_cloudflare = "https://1.1.1.1/dns-query?name=";
        const char *host_google = "dns.google";
        const char *host_cloudflare = "cloudflare-dns.com";

        struct
        {
            const char *url_prefix;
            const char *host_header;
        } servers[] = {
            {url_google, host_google},
            {url_cloudflare, host_cloudflare}};

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            std::string url = servers[attempt].url_prefix + host + "&type=A";
            memory_buffer buf{nullptr, 0};
            CURL *curl = curl_easy_init();
            if (!curl)
                continue;

            // Build Host header
            struct curl_slist *headers = nullptr;
            std::string host_line = "Host: ";
            host_line += servers[attempt].host_header;
            headers = curl_slist_append(headers, host_line.c_str());
            if (!headers)
            {
                curl_easy_cleanup(curl);
                continue;
            }

            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L); // 15 seconds
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "sapp-http/1.0");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_PROXY, ""); // disable proxy
            curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            CURLcode res = curl_easy_perform(curl);
            std::string ip;
            if (res == CURLE_OK && buf.data && buf.size > 0)
            {
                std::string json(buf.data, buf.size);
                size_t pos = json.find("\"data\"");
                if (pos != std::string::npos)
                {
                    pos = json.find(':', pos);
                    if (pos != std::string::npos)
                    {
                        pos = json.find('"', pos);
                        if (pos != std::string::npos)
                        {
                            size_t start = pos + 1;
                            size_t end = json.find('"', start);
                            if (end != std::string::npos)
                            {
                                ip = json.substr(start, end - start);
                            }
                        }
                    }
                }
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            std::free(buf.data);

            if (!ip.empty())
            {
                return ip;
            }
        }

        return ""; // all attempts failed
    }

    static int do_request_get(
        const char *url,
        const sapp_http_header *headers,
        size_t header_count,
        sapp_http_response *out_response)
    {
        if (!url || !*url || !out_response)
            return set_wrapper_error(out_response, SAPPHTTP_E_INVALID_ARGUMENT, "invalid argument");

        clear_response(out_response);

        {
            std::lock_guard<std::mutex> lock(g_init_mutex);
            const int init_code = ensure_initialized_locked();
            if (init_code != SAPPHTTP_OK)
                return set_wrapper_error(out_response, init_code, "curl_global_init failed");
        }

        CURL *curl = curl_easy_init();
        if (!curl)
            return set_wrapper_error(out_response, SAPPHTTP_E_CURL_INIT_FAILED, "curl_easy_init failed");

        // Disable proxy and force IPv4
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // Custom DNS resolution via DoH
        struct curl_slist *resolve_list = nullptr;
        std::string hostname;
        const char *host_start = strstr(url, "://");
        if (host_start)
        {
            host_start += 3;
            const char *host_end = strchr(host_start, '/');
            if (!host_end)
                host_end = host_start + strlen(host_start);
            hostname = std::string(host_start, host_end - host_start);
        }
        size_t colon_pos = hostname.find(':');
        if (colon_pos != std::string::npos)
            hostname = hostname.substr(0, colon_pos);

        if (!hostname.empty() && !std::isdigit(hostname[0]))
        {
            std::string ip = resolve_via_doh(hostname);
            if (!ip.empty())
            {
                int port = 80;
                if (strstr(url, "https://"))
                    port = 443;
                const char *colon = strchr(host_start, ':');
                if (colon && colon < (host_start + hostname.length()))
                {
                    port = atoi(colon + 1);
                }
                char port_str[16];
                snprintf(port_str, sizeof(port_str), "%d", port);
                std::string resolve_str = hostname + ":" + port_str + ":" + ip;
                resolve_list = curl_slist_append(nullptr, resolve_str.c_str());
                if (resolve_list)
                {
                    curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve_list);
                }
            }
        }

        char error_buffer[CURL_ERROR_SIZE] = {0};
        memory_buffer body{nullptr, 0};
        curl_slist *header_list = build_headers(headers, header_count, nullptr);

        if (headers && header_count > 0 && !header_list)
        {
            curl_slist_free_all(resolve_list);
            curl_easy_cleanup(curl);
            return set_wrapper_error(out_response, SAPPHTTP_E_OUT_OF_MEMORY, "failed to build header list");
        }

        CURLcode code = CURLE_OK;
        code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_URL, url);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_USERAGENT, "sapp-http/1.0");
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);

        if (code != CURLE_OK)
        {
            const char *msg = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
            curl_slist_free_all(header_list);
            curl_slist_free_all(resolve_list);
            curl_easy_cleanup(curl);
            std::free(body.data);
            return set_wrapper_error(out_response, SAPPHTTP_E_CURL_OPTION_FAILED, msg);
        }

        code = curl_easy_perform(curl);

        long status = 0;
        const char *content_type = nullptr;
        (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        (void)curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);

        out_response->curl_code = static_cast<int>(code);
        out_response->http_status = status;
        out_response->body_size = body.size;
        out_response->body = body.data;
        out_response->content_type = dup_cstr(content_type);

        if (code != CURLE_OK)
        {
            const char *msg = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
            out_response->error_message = dup_cstr(msg);
        }

        curl_slist_free_all(header_list);
        curl_slist_free_all(resolve_list);
        curl_easy_cleanup(curl);

        if (body.data == nullptr && body.size != 0)
        {
            return set_wrapper_error(out_response, SAPPHTTP_E_OUT_OF_MEMORY, "failed to capture body");
        }
        if (out_response->content_type == nullptr && content_type != nullptr)
        {
            return set_wrapper_error(out_response, SAPPHTTP_E_OUT_OF_MEMORY, "failed to copy content type");
        }
        return SAPPHTTP_OK;
    }

    static int do_request_post(
        const char *url,
        const char *content_type,
        const char *body,
        size_t body_size,
        const sapp_http_header *headers,
        size_t header_count,
        sapp_http_response *out_response)
    {
        if (!url || !*url || !out_response)
            return set_wrapper_error(out_response, SAPPHTTP_E_INVALID_ARGUMENT, "invalid argument");

        clear_response(out_response);

        {
            std::lock_guard<std::mutex> lock(g_init_mutex);
            const int init_code = ensure_initialized_locked();
            if (init_code != SAPPHTTP_OK)
                return set_wrapper_error(out_response, init_code, "curl_global_init failed");
        }

        CURL *curl = curl_easy_init();
        if (!curl)
            return set_wrapper_error(out_response, SAPPHTTP_E_CURL_INIT_FAILED, "curl_easy_init failed");

        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        struct curl_slist *resolve_list = nullptr;
        std::string hostname;
        const char *host_start = strstr(url, "://");
        if (host_start)
        {
            host_start += 3;
            const char *host_end = strchr(host_start, '/');
            if (!host_end)
                host_end = host_start + strlen(host_start);
            hostname = std::string(host_start, host_end - host_start);
        }
        size_t colon_pos = hostname.find(':');
        if (colon_pos != std::string::npos)
            hostname = hostname.substr(0, colon_pos);

        if (!hostname.empty() && !std::isdigit(hostname[0]))
        {
            std::string ip = resolve_via_doh(hostname);
            if (!ip.empty())
            {
                int port = 80;
                if (strstr(url, "https://"))
                    port = 443;
                const char *colon = strchr(host_start, ':');
                if (colon && colon < (host_start + hostname.length()))
                {
                    port = atoi(colon + 1);
                }
                char port_str[16];
                snprintf(port_str, sizeof(port_str), "%d", port);
                std::string resolve_str = hostname + ":" + port_str + ":" + ip;
                resolve_list = curl_slist_append(nullptr, resolve_str.c_str());
                if (resolve_list)
                {
                    curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve_list);
                }
            }
        }

        char error_buffer[CURL_ERROR_SIZE] = {0};
        memory_buffer resp_body{nullptr, 0};
        curl_slist *header_list = build_headers(headers, header_count, content_type);

        if ((headers && header_count > 0) || (content_type && *content_type))
        {
            if (!header_list)
            {
                curl_slist_free_all(resolve_list);
                curl_easy_cleanup(curl);
                return set_wrapper_error(out_response, SAPPHTTP_E_OUT_OF_MEMORY, "failed to build header list");
            }
        }

        CURLcode code = CURLE_OK;
        code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_URL, url);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_USERAGENT, "sapp-http/1.0");
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body_size));
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        if (code == CURLE_OK)
            code = curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);

        if (code != CURLE_OK)
        {
            const char *msg = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
            curl_slist_free_all(header_list);
            curl_slist_free_all(resolve_list);
            curl_easy_cleanup(curl);
            std::free(resp_body.data);
            return set_wrapper_error(out_response, SAPPHTTP_E_CURL_OPTION_FAILED, msg);
        }

        code = curl_easy_perform(curl);

        long status = 0;
        const char *ct = nullptr;
        (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        (void)curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);

        out_response->curl_code = static_cast<int>(code);
        out_response->http_status = status;
        out_response->body_size = resp_body.size;
        out_response->body = resp_body.data;
        out_response->content_type = dup_cstr(ct);

        if (code != CURLE_OK)
        {
            const char *msg = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
            out_response->error_message = dup_cstr(msg);
        }

        curl_slist_free_all(header_list);
        curl_slist_free_all(resolve_list);
        curl_easy_cleanup(curl);

        if (resp_body.data == nullptr && resp_body.size != 0)
        {
            return set_wrapper_error(out_response, SAPPHTTP_E_OUT_OF_MEMORY, "failed to capture body");
        }
        if (out_response->content_type == nullptr && ct != nullptr)
        {
            return set_wrapper_error(out_response, SAPPHTTP_E_OUT_OF_MEMORY, "failed to copy content type");
        }
        return SAPPHTTP_OK;
    }

} // namespace

extern "C"
{

    int SAPPHTTP_CALL sapp_http_global_init(void)
    {
        std::lock_guard<std::mutex> lock(g_init_mutex);
        return ensure_initialized_locked();
    }

    void SAPPHTTP_CALL sapp_http_global_cleanup(void)
    {
        std::lock_guard<std::mutex> lock(g_init_mutex);
        if (g_initialized)
        {
            curl_global_cleanup();
            g_initialized = false;
        }
    }

    int SAPPHTTP_CALL sapp_http_get(
        const char *url,
        const sapp_http_header *headers,
        size_t header_count,
        sapp_http_response *out_response)
    {
        return do_request_get(url, headers, header_count, out_response);
    }

    int SAPPHTTP_CALL sapp_http_post(
        const char *url,
        const char *content_type,
        const char *body,
        size_t body_size,
        const sapp_http_header *headers,
        size_t header_count,
        sapp_http_response *out_response)
    {
        return do_request_post(url, content_type, body, body_size, headers, header_count, out_response);
    }

    void SAPPHTTP_CALL sapp_http_free_response(sapp_http_response *response)
    {
        free_response_members(response);
    }

    const char *SAPPHTTP_CALL sapp_http_version(void)
    {
        return curl_version();
    }

    const char *SAPPHTTP_CALL sapp_http_curl_strerror(int curl_code)
    {
        return curl_easy_strerror(static_cast<CURLcode>(curl_code));
    }

} // extern "C"