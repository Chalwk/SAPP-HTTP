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
int sapp_http_get(const char *url, const sapp_http_header *headers, size_t header_count, sapp_http_response *out_response);
void sapp_http_free_response(sapp_http_response *response);
]]

local function extract_json_value(json, key)
    local pattern = '"' .. key .. '"%s*:%s*"([^"]*)"'
    local start_pos, end_pos, value = string.find(json, pattern)
    if start_pos then return value end
    return nil
end

api_version = "1.12.0.0"

function OnScriptLoad()
    http.sapp_http_global_init() -- start up cURL

    local url = "https://httpbin.org/json"
    local resp = ffi.new("sapp_http_response")

    print("\n--- GET JSON from: " .. url .. " ---")
    local ret = http.sapp_http_get(url, nil, 0, resp) -- fire off a GET request
    print("sapp_http_get returned: " .. ret)

    if ret == 0 and resp.body ~= nil and resp.body ~= ffi.NULL then
        local json = ffi.string(resp.body, resp.body_size)
        print("Raw JSON:")
        print(json)

        -- Simple extraction example
        local slideshow_title = extract_json_value(json, "title")
        if slideshow_title then
            print("\nExtracted title: " .. slideshow_title)
        end
    end

    http.sapp_http_free_response(resp) -- free the response memory
end

function OnScriptUnload()
    http.sapp_http_global_cleanup() -- shut down cURL
end