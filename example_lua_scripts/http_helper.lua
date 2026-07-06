-- Copyright (c) 2026 Jericho Crosby (Chalwk)
-- Save this as a separate file and require it from other scripts

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http = ffi.load("sapp_http")

ffi.cdef(
    [[
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

    int sapp_http_global_init(void);
    void sapp_http_global_cleanup(void);
    const char *sapp_http_version(void);
    const char *sapp_http_curl_strerror(int curl_code);

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
]]
)

local M = {}
local initialized = false

function M.init()
    if not initialized then
        local ret = http.sapp_http_global_init()
        if ret == 0 then
            initialized = true
            print("HTTP helper initialized: " .. ffi.string(http.sapp_http_version()))
        else
            print("HTTP helper init failed: " .. ret)
        end
    end
    return initialized
end

function M.cleanup()
    if initialized then
        http.sapp_http_global_cleanup()
        initialized = false
    end
end

-- GET: returns request handle (or nil)
function M.get(url, headers)
    local header_count = headers and #headers or 0
    return http.sapp_http_create_get(url, headers, header_count)
end

-- POST: returns request handle (or nil)
function M.post(url, content_type, body, headers)
    local header_count = headers and #headers or 0
    return http.sapp_http_create_post(url, content_type, body, #body, headers, header_count)
end

-- PUT: returns request handle (or nil)
function M.put(url, content_type, body, headers)
    local header_count = headers and #headers or 0
    return http.sapp_http_create_put(url, content_type, body, #body, headers, header_count)
end

-- Drive pending transfers
function M.process()
    return http.sapp_http_process()
end

-- Check completion
function M.is_done(req)
    return http.sapp_http_request_is_done(req)
end

-- Retrieve response (returns ret code and response struct)
function M.get_response(req)
    local resp = ffi.new("sapp_http_response")
    local ret = http.sapp_http_request_get_response(req, resp)
    return ret, resp
end

-- Free request handle
function M.free_request(req)
    http.sapp_http_request_free(req)
end

-- Free response struct
function M.free(resp)
    http.sapp_http_free_response(resp)
end

-- Utility
function M.curl_strerror(code)
    return http.sapp_http_curl_strerror(code)
end

return M
