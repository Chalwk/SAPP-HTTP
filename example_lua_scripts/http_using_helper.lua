-- Copyright (c) 2026 Jericho Crosby (Chalwk)

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http_helper = require("http_helper")

api_version = "1.12.0.0"

local request_handle = nil

function OnScriptLoad()
    if not http_helper.init() then return end

    request_handle = http_helper.create_get("https://httpbin.org/get")
    if request_handle == nil then
        print("ERROR: Failed to create GET request")
        return
    end
    timer(100, "CheckHelper")
end

function CheckHelper()
    if request_handle == nil then return end

    http_helper.process()

    if http_helper.is_done(request_handle) == 1 then
        local ret, resp = http_helper.get_response(request_handle)
        http_helper.free_request(request_handle)
        request_handle = nil

        print("GET returned: " .. ret)
        if ret == 0 and resp.body ~= nil and resp.body ~= ffi.NULL then
            print("Body: " .. ffi.string(resp.body, resp.body_size))
        end

        http_helper.free(resp)
    else
        timer(100, "CheckHelper")
    end
end

function OnScriptUnload()
    if request_handle then
        http_helper.free_request(request_handle)
        request_handle = nil
    end
    http_helper.cleanup()
end
