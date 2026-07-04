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

    -- Replace this with your actual Discord webhook URL
    local webhook_url = "https://discord.com/api/webhooks/WEBHOOK_ID/WEBHOOK_TOKEN"

    -- Discord expects JSON with a "content" field
    local message = '{"content": "Hello from SAPP server! http_discord_webhook.lua loaded."}'
    local content_type = "application/json"

    local resp = ffi.new("sapp_http_response")

    print("\n--- Sending Discord webhook ---")
    local ret = http.sapp_http_post(webhook_url, content_type, message, #message, nil, 0, resp) -- fire off a POST request
    print("sapp_http_post returned: " .. ret)

    if ret == 0 then
        if resp.http_status >= 200 and resp.http_status < 300 then
            print("SUCCESS: Webhook sent! (HTTP " .. resp.http_status .. ")")
        else
            print("WARNING: Webhook returned HTTP " .. resp.http_status)
            if resp.body ~= nil and resp.body ~= ffi.NULL then
                print("Response: " .. ffi.string(resp.body, resp.body_size))
            end
        end
    end

    http.sapp_http_free_response(resp) -- free the response memory
end

function OnScriptUnload()
    http.sapp_http_global_cleanup() -- shut down cURL
end