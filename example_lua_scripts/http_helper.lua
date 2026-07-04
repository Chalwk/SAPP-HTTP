-- Copyright (c) 2026 Jericho Crosby (Chalwk)
-- Save this as a separate file and require it from other scripts

local ffi = require("ffi")
local http = ffi.load("sapp_http") -- load the HTTP library

ffi.cdef [[
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

int sapp_http_global_init(void);
void sapp_http_global_cleanup(void);
int sapp_http_get(const char *url, const sapp_http_header *headers, size_t header_count, sapp_http_response *out_response);
int sapp_http_post(const char *url, const char *content_type, const char *body, size_t body_size,
                   const sapp_http_header *headers, size_t header_count, sapp_http_response *out_response);
int sapp_http_put(const char *url, const char *content_type, const char *body, size_t body_size,
                  const sapp_http_header *headers, size_t header_count, sapp_http_response *out_response);
void sapp_http_free_response(sapp_http_response *response);
const char *sapp_http_version(void);
const char *sapp_http_curl_strerror(int curl_code);
]]

local M = {}

-- Initialize once
local initialized = false

function M.init()
    if not initialized then
        local ret = http.sapp_http_global_init() -- start up cURL
        if ret == 0 then
            initialized = true
            print("HTTP helper initialized: " .. http.sapp_http_version())
        else
            print("HTTP helper init failed: " .. ret)
        end
    end
    return initialized
end

function M.cleanup()
    if initialized then
        http.sapp_http_global_cleanup() -- shut down cURL
        initialized = false
    end
end

function M.get(url, headers)
    local resp = ffi.new("sapp_http_response")
    local header_count = headers and #headers or 0
    local ret = http.sapp_http_get(url, headers, header_count, resp) -- fire off a GET request
    return ret, resp
end

function M.post(url, content_type, body, headers)
    local resp = ffi.new("sapp_http_response")
    local header_count = headers and #headers or 0
    local ret = http.sapp_http_post(url, content_type, body, #body, headers, header_count, resp) -- fire off a POST request
    return ret, resp
end

function M.put(url, content_type, body, headers)
    local resp = ffi.new("sapp_http_response")
    local header_count = headers and #headers or 0
    local ret = http.sapp_http_put(url, content_type, body, #body, headers, header_count, resp) -- fire off a PUT request
    return ret, resp
end

function M.free(resp)
    http.sapp_http_free_response(resp) -- free the response memory
end

function M.curl_strerror(code)
    return http.sapp_http_curl_strerror(code)
end

return M