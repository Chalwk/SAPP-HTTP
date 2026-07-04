// Copyright (c) 2026 Jericho Crosby (Chalwk)
// Licensed under the MIT License.

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
#include <memory>
#include <algorithm>

// ------------------------------------------------------------------
//  RAII helpers for libcurl resources
// ------------------------------------------------------------------

namespace
{

    // Simple memory buffer for response body
    struct memory_buffer
    {
        char *data = nullptr;
        size_t size = 0;

        ~memory_buffer() { std::free(data); }

        // Disable copying
        memory_buffer(const memory_buffer &) = delete;
        memory_buffer &operator=(const memory_buffer &) = delete;
        memory_buffer() = default;
    };

    // Wraps a CURL* easy handle
    struct curl_handle
    {
        CURL *handle = nullptr;

        curl_handle() : handle(curl_easy_init()) {}
        ~curl_handle()
        {
            if (handle)
                curl_easy_cleanup(handle);
        }

        // Disable copying
        curl_handle(const curl_handle &) = delete;
        curl_handle &operator=(const curl_handle &) = delete;

        // Allow move
        curl_handle(curl_handle &&other) noexcept : handle(other.handle) { other.handle = nullptr; }
        curl_handle &operator=(curl_handle &&other) noexcept
        {
            if (this != &other)
            {
                if (handle)
                    curl_easy_cleanup(handle);
                handle = other.handle;
                other.handle = nullptr;
            }
            return *this;
        }

        explicit operator bool() const { return handle != nullptr; }
        CURL *get() const { return handle; }
    };

    // Wraps a curl_slist* (linked list of strings)
    struct curl_slist_holder
    {
        curl_slist *list = nullptr;

        ~curl_slist_holder() { curl_slist_free_all(list); }

        // Disable copying
        curl_slist_holder(const curl_slist_holder &) = delete;
        curl_slist_holder &operator=(const curl_slist_holder &) = delete;

        curl_slist_holder() = default;
        explicit curl_slist_holder(curl_slist *l) : list(l) {}

        bool append(const std::string &str)
        {
            curl_slist *new_list = curl_slist_append(list, str.c_str());
            if (!new_list)
                return false;
            list = new_list;
            return true;
        }

        curl_slist *get() const { return list; }
    };

    // ------------------------------------------------------------------
    //  Global state (init / cleanup)
    // ------------------------------------------------------------------

    std::mutex g_init_mutex;
    bool g_initialized = false;

    // ------------------------------------------------------------------
    //  Helper utilities
    // ------------------------------------------------------------------

    static char *dup_cstr(const char *s)
    {
        if (!s)
            return nullptr;
        size_t len = std::strlen(s) + 1;
        char *out = static_cast<char *>(std::malloc(len));
        if (out)
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

    // Sets error fields in response and returns the given error code.
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

    // ------------------------------------------------------------------
    //  libcurl write callback
    // ------------------------------------------------------------------

    static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        size_t total = size * nmemb;
        auto *buf = static_cast<memory_buffer *>(userdata);
        if (!buf || total == 0)
            return 0;

        // Check for overflow
        size_t needed = buf->size + total + 1;
        if (needed < buf->size || needed < total || needed > (std::numeric_limits<size_t>::max)())
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

    // ------------------------------------------------------------------
    //  TODO: refactor this nonsense.
    //
    //  DNS-over-HTTPS resolution (fallback when normal DNS is broken)
    //  Tries Google and Cloudflare, returns first IP found.
    //  Not idea, but I'll keep it simple for now.
    // ------------------------------------------------------------------

    static std::string resolve_via_doh(const std::string &host)
    {
        const char *urls[] = {
            "https://8.8.8.8/resolve?name=",  // Google
            "https://1.1.1.1/dns-query?name=" // Cloudflare
        };
        const char *hosts[] = {
            "dns.google",
            "cloudflare-dns.com"};

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            std::string url = urls[attempt] + host + "&type=A";
            memory_buffer buf;
            curl_handle curl;
            if (!curl)
                continue;

            curl_slist_holder headers;
            std::string host_line = "Host: ";
            host_line += hosts[attempt];
            if (!headers.append(host_line))
                continue;

            curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 15000L);
            curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &buf);
            curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "sapp-http/1.0");
            curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_PROXY, "");
            curl_easy_setopt(curl.get(), CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
            curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

            CURLcode res = curl_easy_perform(curl.get());
            std::string ip;
            if (res == CURLE_OK && buf.data && buf.size > 0)
            {
                std::string json(buf.data, buf.size);
                // Cheap JSON parse - just look for "data":"ip"
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
            if (!ip.empty())
                return ip;
        }
        return "";
    }

    // ------------------------------------------------------------------
    //  Core request function - handles GET, POST, PUT
    // ------------------------------------------------------------------

    enum class HttpMethod
    {
        GET,
        POST,
        PUT
    };

    static int do_request(HttpMethod method,
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

        // Ensure libcurl is globally initialised (should be thread-safe, I hope lol)
        {
            std::lock_guard<std::mutex> lock(g_init_mutex);
            if (!g_initialized)
            {
                CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
                if (code != CURLE_OK)
                    return set_wrapper_error(out_response, SAPPHTTP_E_CURL_INIT_FAILED,
                                             "curl_global_init failed");
                g_initialized = true;
            }
        }

        // Declare slist holders BEFORE the easy handle so they are destroyed after it
        curl_slist_holder resolve_list;
        curl_slist_holder header_list;
        curl_handle curl;
        if (!curl)
            return set_wrapper_error(out_response, SAPPHTTP_E_CURL_INIT_FAILED,
                                     "curl_easy_init failed");

        // Disable proxy and force IPv4 (Halo is 32-bit and may have issues with IPv6)
        curl_easy_setopt(curl.get(), CURLOPT_PROXY, "");
        curl_easy_setopt(curl.get(), CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        // --- DNS-over-HTTPS fallback (if hostname is not an IP) ---
        // We extract the hostname from the URL and try to resolve it via DoH.
        // Might help when the game's environment has broken DNS (common on some servers).
        std::string hostname;
        const char *host_start = strstr(url, "://");
        if (host_start)
        {
            host_start += 3;
            const char *host_end = strchr(host_start, '/');
            if (!host_end)
                host_end = host_start + std::strlen(host_start);
            hostname = std::string(host_start, host_end - host_start);
        }
        size_t colon_pos = hostname.find(':');
        if (colon_pos != std::string::npos)
            hostname = hostname.substr(0, colon_pos);

        // Only resolve if it's not already an IP (starts with digit)
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
                    port = std::atoi(colon + 1);
                }
                char port_str[16];
                snprintf(port_str, sizeof(port_str), "%d", port);
                std::string resolve_str = hostname + ":" + port_str + ":" + ip;
                resolve_list.append(resolve_str); // appends to the list
                curl_easy_setopt(curl.get(), CURLOPT_RESOLVE, resolve_list.get());
            }
        }

        // Build headers (including Content-Type if provided)
        if (content_type && *content_type)
        {
            std::string ct = "Content-Type: ";
            ct += content_type;
            header_list.append(ct);
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
            header_list.append(line);
        }

        // Response buffer
        memory_buffer resp_body;
        char error_buffer[CURL_ERROR_SIZE] = {0};

        // Common libcurl options
        auto set_common_opts = [&]() -> bool
        {
            CURLcode code = CURLE_OK;
            code = curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error_buffer);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_URL, url);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 10L);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "sapp-http/1.0");
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &resp_body);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header_list.get());
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 10000L);
            if (code != CURLE_OK)
                return false;
            code = curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 30000L);
            return code == CURLE_OK;
        };

        if (!set_common_opts())
        {
            const char *msg = error_buffer[0] ? error_buffer : "failed to set libcurl options";
            return set_wrapper_error(out_response, SAPPHTTP_E_CURL_OPTION_FAILED, msg);
        }

        // Method-specific options
        CURLcode code = CURLE_OK;
        switch (method)
        {
        case HttpMethod::GET:
            // nothing extra, break like my heart!
            break;
        case HttpMethod::POST:
            code = curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
            if (code == CURLE_OK)
                code = curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body ? body : "");
            if (code == CURLE_OK)
                code = curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                                        static_cast<curl_off_t>(body_size));
            break;
        case HttpMethod::PUT:
            code = curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "PUT");
            if (code == CURLE_OK)
                code = curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body ? body : "");
            if (code == CURLE_OK)
                code = curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                                        static_cast<curl_off_t>(body_size));
            break;
        }
        if (code != CURLE_OK)
        {
            const char *msg = error_buffer[0] ? error_buffer : "failed to set method options";
            return set_wrapper_error(out_response, SAPPHTTP_E_CURL_OPTION_FAILED, msg);
        }

        // Perform the request
        CURLcode res = curl_easy_perform(curl.get());

        // Detach the slist objects to prevent double-free!
        curl_easy_setopt(curl.get(), CURLOPT_RESOLVE, nullptr);
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, nullptr);

        // Extract response info
        long status = 0;
        const char *ct = nullptr;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &ct);

        out_response->curl_code = static_cast<int>(res);
        out_response->http_status = status;
        out_response->body_size = resp_body.size;
        out_response->body = resp_body.data;
        // Take ownership of the buffer - memory_buffer destructor won't free it now
        resp_body.data = nullptr;
        out_response->content_type = dup_cstr(ct);
        if (res != CURLE_OK)
        {
            const char *msg = error_buffer[0] ? error_buffer : curl_easy_strerror(res);
            out_response->error_message = dup_cstr(msg);
        }

        // Check for allocation failures (dup_cstr, body capture)
        if (out_response->body == nullptr && out_response->body_size != 0)
        {
            free_response_members(out_response);
            return set_wrapper_error(out_response, SAPPHTTP_E_OUT_OF_MEMORY, "failed to capture body");
        }
        if (out_response->content_type == nullptr && ct != nullptr)
        {
            free_response_members(out_response);
            return set_wrapper_error(out_response, SAPPHTTP_E_OUT_OF_MEMORY, "failed to copy content type");
        }

        return SAPPHTTP_OK;
    }

}

// ------------------------------------------------------------------
//  Exported C functions
// ------------------------------------------------------------------

extern "C"
{

    int SAPPHTTP_CALL sapp_http_global_init(void)
    {
        std::lock_guard<std::mutex> lock(g_init_mutex);
        if (g_initialized)
            return SAPPHTTP_OK;
        CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (code != CURLE_OK)
            return SAPPHTTP_E_CURL_INIT_FAILED;
        g_initialized = true;
        return SAPPHTTP_OK;
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

    int SAPPHTTP_CALL sapp_http_get(const char *url,
                                    const sapp_http_header *headers,
                                    size_t header_count,
                                    sapp_http_response *out_response)
    {
        return do_request(HttpMethod::GET, url, nullptr, nullptr, 0, headers, header_count, out_response);
    }

    int SAPPHTTP_CALL sapp_http_post(const char *url,
                                     const char *content_type,
                                     const char *body,
                                     size_t body_size,
                                     const sapp_http_header *headers,
                                     size_t header_count,
                                     sapp_http_response *out_response)
    {
        return do_request(HttpMethod::POST, url, content_type, body, body_size, headers, header_count, out_response);
    }

    int SAPPHTTP_CALL sapp_http_put(const char *url,
                                    const char *content_type,
                                    const char *body,
                                    size_t body_size,
                                    const sapp_http_header *headers,
                                    size_t header_count,
                                    sapp_http_response *out_response)
    {
        return do_request(HttpMethod::PUT, url, content_type, body, body_size, headers, header_count, out_response);
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
}