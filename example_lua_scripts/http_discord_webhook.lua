-- Copyright (c) 2026 Jericho Crosby (Chalwk)

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http = ffi.load("sapp_http")
ffi.cdef(ffi.string(http.sapp_http_get_cdef()))

api_version = "1.12.0.0"

local request_handle = nil
local done = false
local webhook_url = "https://discord.com/api/webhooks/WEBHOOK_ID/WEBHOOK_TOKEN"

function OnScriptLoad()
    http.sapp_http_global_init()

    local message = '{"content": "Hello from SAPP server! http_discord_webhook.lua loaded."}'
    local content_type = "application/json"

    print("\n--- Sending Discord webhook (async) ---")
    request_handle = http.sapp_http_create_post(webhook_url, content_type, message, #message, nil, 0)
    if request_handle == nil then
        print("ERROR: Failed to create POST request")
        return
    end

    timer(100, "CheckWebhook")
end

function CheckWebhook()
    if request_handle == nil then return end

    http.sapp_http_process()

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
        http.sapp_http_free_response(resp)
    else
        timer(100, "CheckWebhook")
    end
end

function OnScriptUnload()
    if request_handle then
        http.sapp_http_request_free(request_handle)
        request_handle = nil
    end
    http.sapp_http_global_cleanup()
end
