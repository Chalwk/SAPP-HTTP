-- Copyright (c) 2026 Jericho Crosby (Chalwk)

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http = ffi.load("sapp_http")
ffi.cdef(ffi.string(http.sapp_http_get_cdef()))

local function extract_json_value(json, key)
    local pattern = '"' .. key .. '"%s*:%s*"([^"]*)"'
    local start_pos, end_pos, value = string.find(json, pattern)
    if start_pos then return value end
    return nil
end

api_version = "1.12.0.0"

local url = "https://httpbin.org/json"
local request_handle = nil

function OnScriptLoad()
    http.sapp_http_global_init()

    print("\n--- GET JSON from: " .. url .. " ---")
    request_handle = http.sapp_http_create_get(url, nil, 0)
    if request_handle == nil then
        print("ERROR: Failed to create GET request")
        return
    end
    timer(100, "CheckJSON")
end

function CheckJSON()
    if request_handle == nil then return end

    http.sapp_http_process()

    if http.sapp_http_request_is_done(request_handle) == 1 then
        local resp = ffi.new("sapp_http_response")
        local ret = http.sapp_http_request_get_response(request_handle, resp)
        http.sapp_http_request_free(request_handle)
        request_handle = nil

        print("sapp_http_get returned: " .. ret)
        if ret == 0 and resp.body ~= nil and resp.body ~= ffi.NULL then
            local json = ffi.string(resp.body, resp.body_size)
            print("Raw JSON:")
            print(json)

            local slideshow_title = extract_json_value(json, "title")
            if slideshow_title then
                print("\nExtracted title: " .. slideshow_title)
            end
        end
        http.sapp_http_free_response(resp)
    else
        timer(100, "CheckJSON")
    end
end

function OnScriptUnload()
    if request_handle then
        http.sapp_http_request_free(request_handle)
        request_handle = nil
    end
    http.sapp_http_global_cleanup()
end
