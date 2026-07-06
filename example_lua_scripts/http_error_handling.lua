-- Copyright (c) 2026 Jericho Crosby (Chalwk)

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http = ffi.load("sapp_http")
ffi.cdef(ffi.string(http.sapp_http_get_cdef()))

local function cstr(ptr)
    if ptr == nil or ptr == ffi.NULL then return "(null)" end
    return ffi.string(ptr)
end

api_version = "1.12.0.0"

local tests = {
    { name = "Test 1: Invalid URL", url = "not_a_valid_url" },
    { name = "Test 2: Non-existent domain", url = "https://this-domain-does-not-exist-12345.com" },
    { name = "Test 3: 404 Not Found", url = "https://httpbin.org/status/404" }
}
local test_index = 0
local request_handle = nil

function OnScriptLoad()
    local init_ret = http.sapp_http_global_init()
    if init_ret ~= 0 then
        print("ERROR: Failed to initialize libcurl: " .. init_ret)
        return
    end

    print("libcurl version: " .. cstr(http.sapp_http_version()))

    run_next_test()
end

function run_next_test()
    test_index = test_index + 1
    if test_index > #tests then
        print("\nAll tests completed.")
        return
    end

    local test = tests[test_index]
    print("\n--- " .. test.name .. " ---")
    request_handle = http.sapp_http_create_get(test.url, nil, 0)
    if request_handle == nil then
        print("ERROR: Failed to create GET request")
        run_next_test()
        return
    end
    timer(100, "CheckTest")
end

function CheckTest()
    if request_handle == nil then return end

    http.sapp_http_process()

    if http.sapp_http_request_is_done(request_handle) == 1 then
        local resp = ffi.new("sapp_http_response")
        local ret = http.sapp_http_request_get_response(request_handle, resp)
        http.sapp_http_request_free(request_handle)
        request_handle = nil

        print("Return code: " .. ret)
        print("curl_code  : " .. resp.curl_code)
        if resp.error_message ~= nil and resp.error_message ~= ffi.NULL then
            print("error_msg  : " .. ffi.string(resp.error_message))
            print("curl_strerror: " .. cstr(http.sapp_http_curl_strerror(resp.curl_code)))
        end
        if resp.body ~= nil and resp.body ~= ffi.NULL then
            print("body       : " .. ffi.string(resp.body, resp.body_size))
        end
        http.sapp_http_free_response(resp)

        timer(500, "run_next_test")
    else
        timer(100, "CheckTest")
    end
end

function OnScriptUnload()
    if request_handle then
        http.sapp_http_request_free(request_handle)
        request_handle = nil
    end
    http.sapp_http_global_cleanup()
end
