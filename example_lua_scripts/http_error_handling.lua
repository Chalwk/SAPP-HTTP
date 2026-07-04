-- Copyright (c) 2026 Jericho Crosby (Chalwk)

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
void sapp_http_free_response(sapp_http_response *response);
const char *sapp_http_curl_strerror(int curl_code);
]]

api_version = "1.12.0.0"

function OnScriptLoad()
    local init_ret = http.sapp_http_global_init() -- start up cURL
    if init_ret ~= 0 then
        print("ERROR: Failed to initialize libcurl: " .. init_ret)
        return
    end

    print("libcurl version: " .. http.sapp_http_version())

    -- Test 1: Invalid URL
    local resp = ffi.new("sapp_http_response")
    print("\n--- Test 1: Invalid URL ---")
    local ret = http.sapp_http_get("not_a_valid_url", nil, 0, resp) -- fire off a GET request
    print("Return code: " .. ret)
    print("curl_code  : " .. resp.curl_code)
    if resp.error_message ~= nil then
        print("error_msg  : " .. resp.error_message)
        print("curl_strerror: " .. http.sapp_http_curl_strerror(resp.curl_code))
    end
    http.sapp_http_free_response(resp) -- free the response memory

    -- Test 2: Non-existent domain
    resp = ffi.new("sapp_http_response")
    print("\n--- Test 2: Non-existent domain ---")
    ret = http.sapp_http_get("https://this-domain-does-not-exist-12345.com", nil, 0, resp) -- fire off a GET request
    print("Return code: " .. ret)
    print("curl_code  : " .. resp.curl_code)
    if resp.error_message ~= nil then
        print("error_msg  : " .. resp.error_message)
        print("curl_strerror: " .. http.sapp_http_curl_strerror(resp.curl_code))
    end
    http.sapp_http_free_response(resp) -- free the response memory

    -- Test 3: 404 Not Found
    resp = ffi.new("sapp_http_response")
    print("\n--- Test 3: 404 Not Found ---")
    ret = http.sapp_http_get("https://httpbin.org/status/404", nil, 0, resp) -- fire off a GET request
    print("Return code: " .. ret)
    print("curl_code  : " .. resp.curl_code)
    print("http_status: " .. resp.http_status)
    if resp.body ~= nil then
        print("body       : " .. ffi.string(resp.body, resp.body_size))
    end
    if resp.error_message ~= nil then
        print("error_msg  : " .. resp.error_message)
    end
    http.sapp_http_free_response(resp) -- free the response memory

    http.sapp_http_global_cleanup() -- shut down cURL
end

function OnScriptUnload() end