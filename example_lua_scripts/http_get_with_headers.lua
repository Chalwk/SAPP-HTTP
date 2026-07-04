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
const char *sapp_http_version(void);
]]

api_version = "1.12.0.0"

function OnScriptLoad()
    http.sapp_http_global_init() -- start up cURL

    -- Build custom headers
    local headers = ffi.new("sapp_http_header[2]")
    headers[0].name = "User-Agent"
    headers[0].value = "SAPP-Server/1.0"
    headers[1].name = "X-Custom-Header"
    headers[1].value = "HelloFromSAPP"

    local url = "https://httpbin.org/headers"
    local resp = ffi.new("sapp_http_response")

    print("\n--- GET with custom headers to: " .. url .. " ---")
    local ret = http.sapp_http_get(url, headers, 2, resp) -- fire off a GET request
    print("sapp_http_get returned: " .. ret)

    if ret == 0 and resp.body ~= nil then
        print("Response body:")
        print(ffi.string(resp.body, resp.body_size))
    end

    http.sapp_http_free_response(resp) -- free the response memory
    http.sapp_http_global_cleanup() -- shut down cURL
end

function OnScriptUnload() end