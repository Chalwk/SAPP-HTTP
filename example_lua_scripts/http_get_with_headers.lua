-- Copyright (c) 2026 Jericho Crosby (Chalwk)

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http = ffi.load("sapp_http")
ffi.cdef(ffi.string(http.sapp_http_get_cdef()))

api_version = "1.12.0.0"

local url = "https://httpbin.org/headers"
local request_handle = nil

function OnScriptLoad()
    http.sapp_http_global_init()

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

        http.sapp_http_free_response(resp)
    else
        timer(100, "CheckHeaders")
    end
end

function OnScriptUnload()
    if request_handle then
        http.sapp_http_request_free(request_handle)
        request_handle = nil
    end
    http.sapp_http_global_cleanup()
end
