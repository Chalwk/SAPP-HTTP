-- Copyright (c) 2026 Jericho Crosby (Chalwk)

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local http = ffi.load("sapp_http")
ffi.cdef(ffi.string(http.sapp_http_get_cdef()))

local function cstr(ptr)
    if ptr == nil or ptr == ffi.NULL then
        return "(null)"
    end
    return ffi.string(ptr)
end

local function print_response(resp)
    print("  curl_code   : " .. resp.curl_code)
    print("  http_status : " .. resp.http_status)
    print("  body_size   : " .. resp.body_size)

    if resp.body ~= nil and resp.body ~= ffi.NULL then
        print("  body        : " .. ffi.string(resp.body, resp.body_size))
    else
        print("  body        : (null)")
    end

    print("  content_type: " .. cstr(resp.content_type))
    print("  error_msg   : " .. cstr(resp.error_message))
end

api_version = "1.12.0.0"

local url = "https://raw.githubusercontent.com/Chalwk/SAPP-HTTP/main/test.txt"
local request_handle = nil

function OnScriptLoad()
    local init_ret = http.sapp_http_global_init()
    if init_ret ~= 0 then
        print("Init failed with code " .. init_ret)
        return
    end

    print("libcurl version: " .. ffi.string(http.sapp_http_version()))

    print("\n--- GET request to: " .. url .. " ---")
    request_handle = http.sapp_http_create_get(url, nil, 0)
    if request_handle == nil then
        print("ERROR: Failed to create GET request")
        return
    end
    timer(100, "CheckText")
end

function CheckText()
    if request_handle == nil then return end

    http.sapp_http_process()

    if http.sapp_http_request_is_done(request_handle) == 1 then
        local resp = ffi.new("sapp_http_response")
        local ret = http.sapp_http_request_get_response(request_handle, resp)
        http.sapp_http_request_free(request_handle)
        request_handle = nil

        print("sapp_http_get returned: " .. ret)
        print_response(resp)

        if ret == 0 and resp.curl_code == 0 and resp.body ~= nil and resp.body ~= ffi.NULL then
            local content = ffi.string(resp.body, resp.body_size)
            if content == "success!" then
                print("\nSUCCESS: Retrieved 'success!' as expected.")
            else
                print("\nWARNING: Unexpected content: " .. content)
            end
        else
            print("\nERROR: Failed to fetch the file.")
            if resp.error_message ~= nil and resp.error_message ~= ffi.NULL then
                print("  curl error: " .. ffi.string(http.sapp_http_curl_strerror(resp.curl_code)))
            end
        end

        http.sapp_http_free_response(resp)
    else
        timer(100, "CheckText")
    end
end

function OnScriptUnload()
    if request_handle then
        http.sapp_http_request_free(request_handle)
        request_handle = nil
    end
    http.sapp_http_global_cleanup()
end
