-- Copyright (c) 2026 Jericho Crosby (Chalwk)

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http = ffi.load("sapp_http")

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

enum sapp_http_status {
    SAPPHTTP_OK = 0,
    SAPPHTTP_E_INVALID_ARGUMENT = -1,
    SAPPHTTP_E_CURL_INIT_FAILED = -2,
    SAPPHTTP_E_CURL_OPTION_FAILED = -3,
    SAPPHTTP_E_OUT_OF_MEMORY = -4
};

int sapp_http_global_init(void);
void sapp_http_global_cleanup(void);

int sapp_http_get(const char *url,
                  const sapp_http_header *headers,
                  size_t header_count,
                  sapp_http_response *out_response);

void sapp_http_free_response(sapp_http_response *response);

const char *sapp_http_version(void);
const char *sapp_http_curl_strerror(int curl_code);
]]

local function print_response(resp)
    print("  curl_code   : " .. resp.curl_code)
    print("  http_status : " .. resp.http_status)
    print("  body_size   : " .. resp.body_size)
    if resp.body ~= nil then
        print("  body        : " .. ffi.string(resp.body, resp.body_size))
    else
        print("  body        : (null)")
    end
    print("  content_type: " .. ((resp.content_type and ffi.string(resp.content_type)) or "(null)"))
    print("  error_msg   : " .. ((resp.error_message and ffi.string(resp.error_message)) or "(none)"))
end

api_version = "1.12.0.0"

function OnScriptLoad()
    local init_ret = http.sapp_http_global_init() -- start up cURL
    if init_ret ~= 0 then
        print("Init failed with code " .. init_ret)
        return
    end

    -- Print libcurl version for verification
    print("libcurl version: " .. ffi.string(http.sapp_http_version()))

    -- Fetch text.txt from GitHub (raw URL)
    local url = "https://raw.githubusercontent.com/Chalwk/SAPP-HTTP/main/text.txt"
    local resp = ffi.new("sapp_http_response")

    print("\n--- GET request to: " .. url .. " ---")
    local ret = http.sapp_http_get(url, nil, 0, resp) -- fire off a GET request
    print("sapp_http_get returned: " .. ret)
    print_response(resp)

    -- Check if the request succeeded and the content matches
    if ret == 0 and resp.curl_code == 0 and resp.body ~= nil then
        local content = ffi.string(resp.body, resp.body_size)
        if content == "success!" then
            print("\nSUCCESS: Retrieved 'success!' as expected.")
        else
            print("\nWARNING: Unexpected content: " .. content)
        end
    else
        print("\nERROR: Failed to fetch the file.")
        if resp.error_message ~= nil then
            print("  curl error: " .. ffi.string(http.sapp_http_curl_strerror(resp.curl_code)))
        end
    end

    http.sapp_http_free_response(resp) -- free the response memory

    http.sapp_http_global_cleanup() -- shut down cURL
end

function OnScriptUnload() end