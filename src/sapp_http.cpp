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
#include <vector>

// ------------------------------------------------------------------
//  RAII helpers for libcurl resources
// ------------------------------------------------------------------

namespace
{

    // Memory buffer for response body
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

        curl_slist_holder() = default;
        explicit curl_slist_holder(curl_slist *l) : list(l) {}

        // Move constructor
        curl_slist_holder(curl_slist_holder &&other) noexcept : list(other.list)
        {
            other.list = nullptr;
        }

        // Move assignment
        curl_slist_holder &operator=(curl_slist_holder &&other) noexcept
        {
            if (this != &other)
            {
                curl_slist_free_all(list);
                list = other.list;
                other.list = nullptr;
            }
            return *this;
        }

        // Delete copy
        curl_slist_holder(const curl_slist_holder &) = delete;
        curl_slist_holder &operator=(const curl_slist_holder &) = delete;

        bool append(const std::string &str)
        {
            curl_slist *new_list = curl_slist_append(list, str.c_str());
            if (!new_list)
                return false;
            list = new_list;
            return true;
        }

        curl_slist *get() const { return list; }
        void detach() { list = nullptr; } // prevent double-free
    };

    // ------------------------------------------------------------------
    //  Global state
    // ------------------------------------------------------------------

    std::mutex g_init_mutex;
    bool g_initialized = false;
    CURLM *g_multi = nullptr;

    // Forward declaration
    struct AsyncRequest;
    std::vector<AsyncRequest *> g_requests;
    std::mutex g_requests_mutex;

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
    //  DNS-over-HTTPS resolution (fallback when normal DNS is broken)
    //  Tries Google and Cloudflare, returns first IP found.
    //  Not ideal, but I'll keep it simple for now.
    // ------------------------------------------------------------------

    static std::string resolve_via_doh(const std::string &host)
    {
        const char *urls[] = {
            "https://8.8.8.8/resolve?name=",
            "https://1.1.1.1/dns-query?name="};
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

    static bool set_common_options(CURL *easy, const char *url, curl_slist *headers,
                                   memory_buffer *resp_buf, char *error_buffer)
    {
        CURLcode code = CURLE_OK;
        code = curl_easy_setopt(easy, CURLOPT_URL, url);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_USERAGENT, "sapp-http/1.0");
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_WRITEDATA, resp_buf);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 30000L);
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_PROXY, "");
        if (code != CURLE_OK)
            goto fail;
        code = curl_easy_setopt(easy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        if (code != CURLE_OK)
            goto fail;
        if (error_buffer)
            code = curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, error_buffer);
        return code == CURLE_OK;
    fail:
        if (error_buffer && error_buffer[0] == '\0')
        {
            snprintf(error_buffer, CURL_ERROR_SIZE, "libcurl option failed");
        }
        return false;
    }

    static curl_slist *build_resolve_list(const char *url, const std::string &hostname)
    {
        if (hostname.empty() || std::isdigit(hostname[0]))
            return nullptr;

        std::string ip = resolve_via_doh(hostname);
        if (ip.empty())
            return nullptr;

        int port = 80;
        if (strstr(url, "https://"))
            port = 443;
        const char *host_start = strstr(url, "://");
        if (host_start)
        {
            host_start += 3;
            const char *colon = strchr(host_start, ':');
            if (colon)
            {
                port = std::atoi(colon + 1);
            }
        }
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);
        std::string resolve_str = hostname + ":" + port_str + ":" + ip;
        return curl_slist_append(nullptr, resolve_str.c_str());
    }

    // ------------------------------------------------------------------
    //  Core synchronous request
    // ------------------------------------------------------------------

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

        curl_slist_holder resolve_list;
        curl_slist_holder header_list;
        curl_handle curl;
        if (!curl)
            return set_wrapper_error(out_response, SAPPHTTP_E_CURL_INIT_FAILED,
                                     "curl_easy_init failed");

        // DNS-over-HTTPS fallback
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

        if (!hostname.empty() && !std::isdigit(hostname[0]))
        {
            curl_slist *resolve = build_resolve_list(url, hostname);
            if (resolve)
            {
                resolve_list = curl_slist_holder(resolve); // move assignment
                curl_easy_setopt(curl.get(), CURLOPT_RESOLVE, resolve_list.get());
            }
        }

        // Build headers
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

        memory_buffer resp_body;
        char error_buffer[CURL_ERROR_SIZE] = {0};

        if (!set_common_options(curl.get(), url, header_list.get(), &resp_body, error_buffer))
        {
            const char *msg = error_buffer[0] ? error_buffer : "failed to set libcurl options";
            return set_wrapper_error(out_response, SAPPHTTP_E_CURL_OPTION_FAILED, msg);
        }

        // Method-specific
        CURLcode code = CURLE_OK;
        switch (method)
        {
        case HttpMethod::GET:
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

        CURLcode res = curl_easy_perform(curl.get());

        long status = 0;
        const char *ct = nullptr;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &ct);

        out_response->curl_code = static_cast<int>(res);
        out_response->http_status = status;
        out_response->body_size = resp_body.size;
        out_response->body = resp_body.data;
        resp_body.data = nullptr;
        out_response->content_type = dup_cstr(ct);
        if (res != CURLE_OK)
        {
            const char *msg = error_buffer[0] ? error_buffer : curl_easy_strerror(res);
            out_response->error_message = dup_cstr(msg);
        }

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

    // ------------------------------------------------------------------
    //  Asynchronous request internals
    // ------------------------------------------------------------------

    struct AsyncRequest
    {
        CURL *easy = nullptr;
        curl_slist *headers = nullptr;
        curl_slist *resolve_list = nullptr;
        memory_buffer response;
        char *content_type = nullptr;
        char *error_message = nullptr;
        long http_status = 0;
        CURLcode curl_code = CURLE_OK;
        bool done = false;
        bool active = false;
        char *request_body = nullptr;
        size_t request_body_size = 0;
    };

    static AsyncRequest *create_async_request(HttpMethod method,
                                              const char *url,
                                              const char *content_type,
                                              const char *body,
                                              size_t body_size,
                                              const sapp_http_header *headers,
                                              size_t header_count)
    {
        if (!url || !*url)
            return nullptr;

        AsyncRequest *req = new AsyncRequest();
        req->easy = curl_easy_init();
        if (!req->easy)
        {
            delete req;
            return nullptr;
        }

        // Copy body if provided
        if (body && body_size > 0)
        {
            req->request_body = static_cast<char *>(std::malloc(body_size));
            if (!req->request_body)
            {
                curl_easy_cleanup(req->easy);
                delete req;
                return nullptr;
            }
            std::memcpy(req->request_body, body, body_size);
            req->request_body_size = body_size;
        }

        // Build header list
        curl_slist *hlist = nullptr;
        if (content_type && *content_type)
        {
            std::string ct = "Content-Type: ";
            ct += content_type;
            hlist = curl_slist_append(hlist, ct.c_str());
            if (!hlist)
            {
                curl_easy_cleanup(req->easy);
                std::free(req->request_body);
                delete req;
                return nullptr;
            }
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
            curl_slist *new_list = curl_slist_append(hlist, line.c_str());
            if (!new_list)
            {
                curl_slist_free_all(hlist);
                curl_easy_cleanup(req->easy);
                std::free(req->request_body);
                delete req;
                return nullptr;
            }
            hlist = new_list;
        }
        req->headers = hlist;

        // DNS fallback
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

        if (!hostname.empty() && !std::isdigit(hostname[0]))
        {
            req->resolve_list = build_resolve_list(url, hostname);
            if (req->resolve_list)
            {
                curl_easy_setopt(req->easy, CURLOPT_RESOLVE, req->resolve_list);
            }
        }

        char error_buffer[CURL_ERROR_SIZE] = {0};
        if (!set_common_options(req->easy, url, req->headers, &req->response, error_buffer))
        {
            curl_slist_free_all(req->headers);
            curl_slist_free_all(req->resolve_list);
            curl_easy_cleanup(req->easy);
            std::free(req->request_body);
            delete req;
            return nullptr;
        }

        // Method-specific
        CURLcode code = CURLE_OK;
        switch (method)
        {
        case HttpMethod::GET:
            break;
        case HttpMethod::POST:
            code = curl_easy_setopt(req->easy, CURLOPT_POST, 1L);
            if (code == CURLE_OK)
                code = curl_easy_setopt(req->easy, CURLOPT_POSTFIELDS,
                                        req->request_body ? req->request_body : "");
            if (code == CURLE_OK)
                code = curl_easy_setopt(req->easy, CURLOPT_POSTFIELDSIZE_LARGE,
                                        static_cast<curl_off_t>(req->request_body_size));
            break;
        case HttpMethod::PUT:
            code = curl_easy_setopt(req->easy, CURLOPT_CUSTOMREQUEST, "PUT");
            if (code == CURLE_OK)
                code = curl_easy_setopt(req->easy, CURLOPT_POSTFIELDS,
                                        req->request_body ? req->request_body : "");
            if (code == CURLE_OK)
                code = curl_easy_setopt(req->easy, CURLOPT_POSTFIELDSIZE_LARGE,
                                        static_cast<curl_off_t>(req->request_body_size));
            break;
        }
        if (code != CURLE_OK)
        {
            curl_slist_free_all(req->headers);
            curl_slist_free_all(req->resolve_list);
            curl_easy_cleanup(req->easy);
            std::free(req->request_body);
            delete req;
            return nullptr;
        }

        // Store pointer in easy handle for lookup (use CURLINFO_PRIVATE when reading)
        curl_easy_setopt(req->easy, CURLOPT_PRIVATE, req);

        {
            std::lock_guard<std::mutex> lock(g_requests_mutex);
            if (!g_multi)
            {
                curl_easy_cleanup(req->easy);
                delete req;
                return nullptr;
            }
            CURLMcode mcode = curl_multi_add_handle(g_multi, req->easy);
            if (mcode != CURLM_OK)
            {
                curl_easy_cleanup(req->easy);
                delete req;
                return nullptr;
            }
            req->active = true;
            g_requests.push_back(req);
        }

        return req;
    }

} // namespace

// ------------------------------------------------------------------
//  Exported synchronous C functions
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

        g_multi = curl_multi_init();
        if (!g_multi)
        {
            curl_global_cleanup();
            return SAPPHTTP_E_CURL_INIT_FAILED;
        }

        g_initialized = true;
        return SAPPHTTP_OK;
    }

    void SAPPHTTP_CALL sapp_http_global_cleanup(void)
    {
        std::lock_guard<std::mutex> lock(g_init_mutex);
        if (!g_initialized)
            return;

        // Clean up all pending async requests
        {
            std::lock_guard<std::mutex> req_lock(g_requests_mutex);
            for (AsyncRequest *req : g_requests)
            {
                if (req->active && g_multi)
                {
                    curl_multi_remove_handle(g_multi, req->easy);
                    req->active = false;
                }
                if (req->easy)
                    curl_easy_cleanup(req->easy);
                curl_slist_free_all(req->headers);
                curl_slist_free_all(req->resolve_list);
                std::free(req->request_body);
                std::free(req->content_type);
                std::free(req->error_message);
                delete req;
            }
            g_requests.clear();
        }

        if (g_multi)
        {
            curl_multi_cleanup(g_multi);
            g_multi = nullptr;
        }
        curl_global_cleanup();
        g_initialized = false;
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

    // ------------------------------------------------------------------
    //  Exported asynchronous C functions
    // ------------------------------------------------------------------

    SAPPHTTP_API sapp_http_request *SAPPHTTP_CALL sapp_http_create_get(
        const char *url,
        const sapp_http_header *headers,
        size_t header_count)
    {
        AsyncRequest *req = create_async_request(HttpMethod::GET, url, nullptr, nullptr, 0,
                                                 headers, header_count);
        return reinterpret_cast<sapp_http_request *>(req);
    }

    SAPPHTTP_API sapp_http_request *SAPPHTTP_CALL sapp_http_create_post(
        const char *url,
        const char *content_type,
        const char *body,
        size_t body_size,
        const sapp_http_header *headers,
        size_t header_count)
    {
        AsyncRequest *req = create_async_request(HttpMethod::POST, url, content_type,
                                                 body, body_size, headers, header_count);
        return reinterpret_cast<sapp_http_request *>(req);
    }

    SAPPHTTP_API sapp_http_request *SAPPHTTP_CALL sapp_http_create_put(
        const char *url,
        const char *content_type,
        const char *body,
        size_t body_size,
        const sapp_http_header *headers,
        size_t header_count)
    {
        AsyncRequest *req = create_async_request(HttpMethod::PUT, url, content_type,
                                                 body, body_size, headers, header_count);
        return reinterpret_cast<sapp_http_request *>(req);
    }

    SAPPHTTP_API int SAPPHTTP_CALL sapp_http_process(void)
    {
        if (!g_multi)
            return SAPPHTTP_E_CURL_INIT_FAILED;

        int still_running = 0;
        CURLMcode mcode = curl_multi_perform(g_multi, &still_running);
        if (mcode != CURLM_OK)
            return -static_cast<int>(mcode);

        // Check for completed transfers
        int msgs_left;
        CURLMsg *msg;
        while ((msg = curl_multi_info_read(g_multi, &msgs_left)) != nullptr)
        {
            if (msg->msg == CURLMSG_DONE)
            {
                CURL *easy = msg->easy_handle;
                AsyncRequest *req = nullptr;
                // Use CURLINFO_PRIVATE to retrieve the stored pointer
                curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);
                if (req)
                {
                    long status = 0;
                    const char *ct = nullptr;
                    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
                    curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &ct);

                    req->http_status = status;
                    req->curl_code = msg->data.result;
                    req->content_type = dup_cstr(ct);
                    if (msg->data.result != CURLE_OK)
                    {
                        const char *err = curl_easy_strerror(msg->data.result);
                        req->error_message = dup_cstr(err);
                    }

                    // Remove from multi handle
                    curl_multi_remove_handle(g_multi, easy);
                    req->active = false;
                    req->done = true;
                }
            }
        }

        return still_running;
    }

    SAPPHTTP_API int SAPPHTTP_CALL sapp_http_request_is_done(sapp_http_request *req)
    {
        if (!req)
            return SAPPHTTP_E_INVALID_ARGUMENT;
        AsyncRequest *req_impl = reinterpret_cast<AsyncRequest *>(req);
        return req_impl->done ? 1 : 0;
    }

    SAPPHTTP_API int SAPPHTTP_CALL sapp_http_request_get_response(
        sapp_http_request *req,
        sapp_http_response *out)
    {
        if (!req || !out)
            return SAPPHTTP_E_INVALID_ARGUMENT;
        AsyncRequest *req_impl = reinterpret_cast<AsyncRequest *>(req);
        if (!req_impl->done)
            return SAPPHTTP_E_INVALID_ARGUMENT; // not done

        clear_response(out);
        out->curl_code = static_cast<int>(req_impl->curl_code);
        out->http_status = req_impl->http_status;
        out->body_size = req_impl->response.size;
        out->body = req_impl->response.data;
        req_impl->response.data = nullptr; // transfer ownership
        out->content_type = dup_cstr(req_impl->content_type);
        out->error_message = dup_cstr(req_impl->error_message);

        // Check allocation failures
        if ((out->body == nullptr && out->body_size != 0) ||
            (out->content_type == nullptr && req_impl->content_type != nullptr) ||
            (out->error_message == nullptr && req_impl->error_message != nullptr))
        {
            free_response_members(out);
            return SAPPHTTP_E_OUT_OF_MEMORY;
        }
        return SAPPHTTP_OK;
    }

    SAPPHTTP_API void SAPPHTTP_CALL sapp_http_request_free(sapp_http_request *req)
    {
        if (!req)
            return;

        AsyncRequest *req_impl = reinterpret_cast<AsyncRequest *>(req);

        // Remove from multi if still active
        if (req_impl->active && g_multi)
        {
            curl_multi_remove_handle(g_multi, req_impl->easy);
            req_impl->active = false;
        }

        // Clean up libcurl resources
        if (req_impl->easy)
        {
            curl_easy_cleanup(req_impl->easy);
            req_impl->easy = nullptr;
        }
        curl_slist_free_all(req_impl->headers);
        curl_slist_free_all(req_impl->resolve_list);
        std::free(req_impl->request_body);
        std::free(req_impl->content_type);
        std::free(req_impl->error_message);
        // response buffer is freed by memory_buffer destructor (if not transferred)

        // Remove from global list
        {
            std::lock_guard<std::mutex> lock(g_requests_mutex);
            auto it = std::find(g_requests.begin(), g_requests.end(), req_impl);
            if (it != g_requests.end())
                g_requests.erase(it);
        }

        delete req_impl;
    }

    // ------------------------------------------------------------------
    //  Returns the full ffi.cdef declaration as a C string to streamline
    //  Lua scripts. This eliminates the need for each script to repeat
    //  the large cdef block, reducing boilerplate and maintenance.
    // ------------------------------------------------------------------
    SAPPHTTP_API const char *SAPPHTTP_CALL sapp_http_get_cdef(void)
    {
        static const char *cdef = R"(
typedef struct sapp_http_header {
    const char *name;
    const char *value;
} sapp_http_header;

typedef struct sapp_http_response {
    int curl_code;
    long http_status;
    size_t body_size;
    char *body;
    char *content_type;
    char *error_message;
} sapp_http_response;

typedef struct sapp_http_request sapp_http_request;

enum sapp_http_status {
    SAPPHTTP_OK = 0,
    SAPPHTTP_E_INVALID_ARGUMENT = -1,
    SAPPHTTP_E_CURL_INIT_FAILED = -2,
    SAPPHTTP_E_CURL_OPTION_FAILED = -3,
    SAPPHTTP_E_OUT_OF_MEMORY = -4
};

int sapp_http_global_init(void);
void sapp_http_global_cleanup(void);

sapp_http_request* sapp_http_create_get(const char *url,
                                        const sapp_http_header *headers,
                                        size_t header_count);
sapp_http_request* sapp_http_create_post(const char *url,
                                         const char *content_type,
                                         const char *body,
                                         size_t body_size,
                                         const sapp_http_header *headers,
                                         size_t header_count);
sapp_http_request* sapp_http_create_put(const char *url,
                                        const char *content_type,
                                        const char *body,
                                        size_t body_size,
                                        const sapp_http_header *headers,
                                        size_t header_count);
int sapp_http_process(void);
int sapp_http_request_is_done(sapp_http_request *req);
int sapp_http_request_get_response(sapp_http_request *req,
                                   sapp_http_response *out);
void sapp_http_request_free(sapp_http_request *req);
void sapp_http_free_response(sapp_http_response *response);
const char *sapp_http_version(void);
const char *sapp_http_curl_strerror(int curl_code);
)";
        return cdef;
    }
}