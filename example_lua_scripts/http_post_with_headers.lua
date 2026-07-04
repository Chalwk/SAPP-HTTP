-- Copyright (c) 2026 Jericho Crosby (Chalwk)

---@diagnostic disable-next-line: unresolved-require
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
int sapp_http_post(const char *url, const char *content_type, const char *body, size_t body_size,
                   const sapp_http_header *headers, size_t header_count, sapp_http_response *out_response);
void sapp_http_free_response(sapp_http_response *response);
]]

api_version = "1.12.0.0"

function OnScriptLoad()
    http.sapp_http_global_init() -- start up cURL

    -- Custom headers
    local headers = ffi.new("sapp_http_header[2]")
    headers[0].name = "X-API-Key"
    headers[0].value = "secret-key-123"
    headers[1].name = "X-Request-ID"
    headers[1].value = "sapp-001"

    local url = "https://httpbin.org/post"
    local json_body = '{"status": "testing"}'
    local content_type = "application/json"

    local resp = ffi.new("sapp_http_response")

    print("\n--- POST with custom headers to: " .. url .. " ---")
    local ret = http.sapp_http_post(url, content_type, json_body, #json_body, headers, 2, resp) -- fire off a POST request
    print("sapp_http_post returned: " .. ret)

    if ret == 0 and resp.body ~= nil and resp.body ~= ffi.NULL then
        print("Response:")
        print(ffi.string(resp.body, resp.body_size))
    end

    http.sapp_http_free_response(resp) -- free the response memory
end

function OnScriptUnload()
    http.sapp_http_global_cleanup() -- shut down cURL
end