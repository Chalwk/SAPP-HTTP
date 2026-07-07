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
    //  Common option setup for async requests
    // ------------------------------------------------------------------

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
        code = curl_easy_setopt(easy, CURLOPT_USERAGENT, "sapp-http/1.0.10");
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

    // ------------------------------------------------------------------
    //  Asynchronous request internals
    // ------------------------------------------------------------------

    enum class HttpMethod
    {
        GET,
        POST,
        PUT
    };

    struct AsyncRequest
    {
        CURL *easy = nullptr;
        curl_slist *headers = nullptr;
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

        char error_buffer[CURL_ERROR_SIZE] = {0};
        if (!set_common_options(req->easy, url, req->headers, &req->response, error_buffer))
        {
            curl_slist_free_all(req->headers);
            curl_easy_cleanup(req->easy);
            std::free(req->request_body);
            delete req;
            return nullptr;
        }

        // Method-specific options
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
            curl_easy_cleanup(req->easy);
            std::free(req->request_body);
            delete req;
            return nullptr;
        }

        // Store pointer in easy handle
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
    //  Asynchronous API
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
        std::free(req_impl->request_body);
        std::free(req_impl->content_type);
        std::free(req_impl->error_message);
        // response buffer is freed by memory_buffer destructor (if not transferred)

        {
            std::lock_guard<std::mutex> lock(g_requests_mutex);
            auto it = std::find(g_requests.begin(), g_requests.end(), req_impl);
            if (it != g_requests.end())
                g_requests.erase(it);
        }

        delete req_impl;
    }
}