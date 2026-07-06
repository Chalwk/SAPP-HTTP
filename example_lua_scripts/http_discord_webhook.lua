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

sapp_http_request* sapp_http_create_post(const char *url,
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

api_version = "1.12.0.0"

local request_handle = nil
local done = false
local webhook_url = "https://discord.com/api/webhooks/WEBHOOK_ID/WEBHOOK_TOKEN"

function OnScriptLoad()
    http.sapp_http_global_init() -- start up cURL

    -- Discord expects JSON with a "content" field
    local message = '{"content": "Hello from SAPP server! http_discord_webhook.lua loaded."}'
    local content_type = "application/json"

    print("\n--- Sending Discord webhook (async) ---")
    request_handle = http.sapp_http_create_post(webhook_url, content_type, message, #message, nil, 0)
    if request_handle == nil then
        print("ERROR: Failed to create POST request")
        return
    end

    -- Start polling timer (every 100ms)
    timer(100, "CheckWebhook")
end

function CheckWebhook()
    if request_handle == nil then return end

    http.sapp_http_process() -- drive the request

    if http.sapp_http_request_is_done(request_handle) == 1 then
        local resp = ffi.new("sapp_http_response")
        local ret = http.sapp_http_request_get_response(request_handle, resp)
        http.sapp_http_request_free(request_handle)
        request_handle = nil

        if ret == 0 then
            if resp.http_status >= 200 and resp.http_status < 300 then
                print("SUCCESS: Webhook sent! (HTTP " .. resp.http_status .. ")")
            else
                print("WARNING: Webhook returned HTTP " .. resp.http_status)
                if resp.body ~= nil and resp.body ~= ffi.NULL then
                    print("Response: " .. ffi.string(resp.body, resp.body_size))
                end
            end
        else
            print("ERROR: Request failed, ret = " .. ret)
            if resp.error_message ~= nil and resp.error_message ~= ffi.NULL then
                print("Error: " .. ffi.string(resp.error_message))
            end
        end
        http.sapp_http_free_response(resp) -- free the response memory
    else
        timer(100, "CheckWebhook") -- poll again
    end
end

function OnScriptUnload()
    if request_handle then
        http.sapp_http_request_free(request_handle)
        request_handle = nil
    end
    http.sapp_http_global_cleanup() -- shut down cURL
end
