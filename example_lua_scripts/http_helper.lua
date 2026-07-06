-- Copyright (c) 2026 Jericho Crosby (Chalwk)
-- Save this as a separate file and require it from other scripts

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http = ffi.load("sapp_http")
ffi.cdef(ffi.string(http.sapp_http_get_cdef()))

local M = {}

local initialized = false

function M.init()
    if not initialized then
        local ret = http.sapp_http_global_init()
        if ret == 0 then
            initialized = true
            print("HTTP helper initialized: " .. ffi.string(http.sapp_http_version()))
        else
            print("HTTP helper init failed: " .. ret)
        end
    end
    return initialized
end

function M.cleanup()
    if initialized then
        http.sapp_http_global_cleanup()
        initialized = false
    end
end

function M.get(url, headers)
    local resp = ffi.new("sapp_http_response")
    local header_count = headers and #headers or 0
    local ret = http.sapp_http_get(url, headers, header_count, resp)
    return ret, resp
end

function M.post(url, content_type, body, headers)
    local resp = ffi.new("sapp_http_response")
    local header_count = headers and #headers or 0
    local ret = http.sapp_http_post(url, content_type, body, #body, headers, header_count, resp)
    return ret, resp
end

function M.put(url, content_type, body, headers)
    local resp = ffi.new("sapp_http_response")
    local header_count = headers and #headers or 0
    local ret = http.sapp_http_put(url, content_type, body, #body, headers, header_count, resp)
    return ret, resp
end

function M.free(resp)
    http.sapp_http_free_response(resp)
end

function M.curl_strerror(code)
    return http.sapp_http_curl_strerror(code)
end

function M.create_get(url, headers)
    local header_count = headers and #headers or 0
    return http.sapp_http_create_get(url, headers, header_count)
end

function M.create_post(url, content_type, body, headers)
    local header_count = headers and #headers or 0
    return http.sapp_http_create_post(url, content_type, body, #body, headers, header_count)
end

function M.create_put(url, content_type, body, headers)
    local header_count = headers and #headers or 0
    return http.sapp_http_create_put(url, content_type, body, #body, headers, header_count)
end

function M.process()
    return http.sapp_http_process()
end

function M.is_done(req)
    return http.sapp_http_request_is_done(req)
end

function M.get_response(req)
    local resp = ffi.new("sapp_http_response")
    local ret = http.sapp_http_request_get_response(req, resp)
    return ret, resp
end

function M.free_request(req)
    http.sapp_http_request_free(req)
end

return M
