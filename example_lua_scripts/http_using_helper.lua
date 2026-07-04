-- Copyright (c) 2026 Jericho Crosby (Chalwk)

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http_helper = require("http_helper")

api_version = "1.12.0.0"

function OnScriptLoad()
    if not http_helper.init() then return end

    local ret, resp = http_helper.get("https://httpbin.org/get")
    print("GET returned: " .. ret)

    if ret == 0 and resp.body ~= nil and resp.body ~= ffi.NULL then
        print("Body: " .. ffi.string(resp.body, resp.body_size))
    end

    http_helper.free(resp) -- free the response memory
end

function OnScriptUnload()
    http_helper.cleanup() -- shut down cURL
end
