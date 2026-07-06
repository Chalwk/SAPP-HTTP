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

typedef struct sapp_http_request sapp_http_request;

int sapp_http_global_init(void);
void sapp_http_global_cleanup(void);

sapp_http_request* sapp_http_create_get(const char *url,
                                        const sapp_http_header *headers,
                                        size_t header_count);
int sapp_http_process(void);
int sapp_http_request_is_done(sapp_http_request *req);
int sapp_http_request_get_response(sapp_http_request *req,
                                   sapp_http_response *out);
void sapp_http_request_free(sapp_http_request *req);
void sapp_http_free_response(sapp_http_response *response);
const char *sapp_http_version(void);
]]

api_version = "1.12.0.0"

local url = "https://httpbin.org/headers"
local request_handle = nil

function OnScriptLoad()
    http.sapp_http_global_init() -- start up cURL

    -- Build custom headers
    local headers = ffi.new("sapp_http_header[2]")
    headers[0].name = "User-Agent"
    headers[0].value = "SAPP-Server/1.0"
    headers[1].name = "X-Custom-Header"
    headers[1].value = "HelloFromSAPP"

    print("\n--- GET with custom headers to: " .. url .. " ---")
    request_handle = http.sapp_http_create_get(url, headers, 2)
    if request_handle == nil then
        print("ERROR: Failed to create GET request")
        return
    end
    timer(100, "CheckHeaders")
end

function CheckHeaders()
    if request_handle == nil then return end

    http.sapp_http_process()

    if http.sapp_http_request_is_done(request_handle) == 1 then
        local resp = ffi.new("sapp_http_response")
        local ret = http.sapp_http_request_get_response(request_handle, resp)
        http.sapp_http_request_free(request_handle)
        request_handle = nil

        print("sapp_http_get returned: " .. ret)
        if ret == 0 and resp.body ~= nil and resp.body ~= ffi.NULL then
            print("Response body:")
            print(ffi.string(resp.body, resp.body_size))
        end

        http.sapp_http_free_response(resp) -- free the response memory
    else
        timer(100, "CheckHeaders")
    end
end

function OnScriptUnload()
    if request_handle then
        http.sapp_http_request_free(request_handle)
        request_handle = nil
    end
    http.sapp_http_global_cleanup() -- shut down cURL
end