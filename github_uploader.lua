-- Copyright (c) 2026 Jericho Crosby (Chalwk)
--
-- Uploads ranks.txt to GitHub on every change.
-- Throttled to at most one upload every 12 hours.

api_version = "1.12.0.0"

---@diagnostic disable-next-line: unresolved-require
local ffi = require("ffi")
local C = ffi.C
local sapp_http = ffi.load("sapp_http")
ffi.cdef(ffi.string(sapp_http.sapp_http_get_cdef()))

-- ------------------------------
-- Configuration
-- ------------------------------
local GITHUB_OWNER = "Chalwk"
local GITHUB_REPO = "liberty-ce"
local GITHUB_PATH = "data/ranks.txt"
local GITHUB_TOKEN = "REDACTED"
local COMMIT_MSG = "Auto-update ranks.txt from game server"
local LOCAL_FILE = "ranks.txt"
local POLL_SECONDS = 43200     -- check file every 12 hours
local FAST_INTERVAL = 500      -- process network every 0.5s
local RETRY_DELAY = 5          -- initial delay in seconds
local MAX_RETRIES = 3

-- --------------------------------
-- Local references for performance
-- --------------------------------
local io_open = io.open
local byte = string.byte
local math_floor = math.floor
local table_concat = table.concat
local string_format = string.format

-- ------------------------------
-- Windows file time helpers
-- ------------------------------
ffi.cdef(
    [[
    typedef unsigned long DWORD;
    typedef unsigned long long ULONGLONG;
    typedef struct _FILETIME {
        DWORD dwLowDateTime;
        DWORD dwHighDateTime;
    } FILETIME;

    void * __stdcall CreateFileA(const char *lpFileName,
                                 DWORD dwDesiredAccess,
                                 DWORD dwShareMode,
                                 void *lpSecurityAttributes,
                                 DWORD dwCreationDisposition,
                                 DWORD dwFlagsAndAttributes,
                                 void *hTemplateFile);
    int __stdcall GetFileTime(void *hFile,
                              void *lpCreationTime,
                              void *lpLastAccessTime,
                              FILETIME *lpLastWriteTime);
    int __stdcall CloseHandle(void *hObject);
]]
)

local INVALID_HANDLE_VALUE = ffi.cast("void *", -1)
local GENERIC_READ = 0x80000000
local FILE_SHARE_READ = 0x00000001
local OPEN_EXISTING = 3

-- ------------------------------
-- Base64 encoder
-- ------------------------------
local b64chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
local function base64_encode(data)
    local result = {}
    local len = #data
    local i = 1
    while i <= len do
        local a, b, c = byte(data, i, i + 2)
        local n = (a or 0) * 0x10000 + (b or 0) * 0x100 + (c or 0)
        local pads = 0
        if i + 1 > len then
            pads = 2
        elseif i + 2 > len then
            pads = 1
        end
        for _ = 1, 4 - pads do
            local idx = math_floor(n / 0x40000) + 1
            result[#result + 1] = b64chars:sub(idx, idx)
            n = (n % 0x40000) * 0x40
        end
        for _ = 1, pads do
            result[#result + 1] = '='
        end
        i = i + 3
    end
    return table_concat(result)
end

-- ------------------------------
-- File change detection
-- ------------------------------
local last_ts = 0

local function get_file_info(path)
    local h = C.CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nil, OPEN_EXISTING, 0, nil)
    if h == INVALID_HANDLE_VALUE then
        return nil, nil, "File not found or inaccessible"
    end

    local ft = ffi.new("FILETIME[1]")
    local ok = C.GetFileTime(h, nil, nil, ft)
    C.CloseHandle(h)
    if ok == 0 then
        return nil, nil, "GetFileTime failed"
    end

    local f = io_open(path, "rb")
    if not f then
        return nil, nil, "Failed to open file for reading"
    end
    local content = f:read("*all")
    f:close()

    local ts = ft[0].dwLowDateTime + ft[0].dwHighDateTime * 0x100000000
    return content, ts, nil
end

-- ------------------------------
-- State machine for async upload
-- ------------------------------
local state = "idle"        -- idle | get_pending | get_retry | put_pending | put_retry
local current_request = nil -- sapp_http_request*
local current_sha = nil     -- SHA from GET response
local current_content = nil -- file content to upload
local retry_count = 0
local last_upload_time = 0 -- timestamp of last successful upload (for throttling)

-- Helper to build common headers (Accept, User-Agent, Authorization)
local function build_github_headers()
    local auth_val = "token " .. GITHUB_TOKEN
    local headers = ffi.new("sapp_http_header[3]")
    headers[0].name = "Accept"
    headers[0].value = "application/vnd.github+json"
    headers[1].name = "User-Agent"
    headers[1].value = "sapp-http/1.0"
    headers[2].name = "Authorization"
    headers[2].value = auth_val
    return headers, 3
end

-- Cleanup and reset state (does NOT reset last_upload_time)
local function reset_state()
    if current_request then
        sapp_http.sapp_http_request_free(current_request)
        current_request = nil
    end
    state = "idle"
    current_sha = nil
    current_content = nil
    retry_count = 0
end

-- This function is called from the fast timer.
function CheckAndUpload()
    -- 1. Process any pending network activity (non‑blocking)
    local still = sapp_http.sapp_http_process()
    if still < 0 then
        print("[GitHub Uploader] sapp_http_process error: " .. tostring(still))
        return
    end

    -- 2. State machine
    if state == "idle" then
        -- Throttle: only check file changes if enough time has passed
        local now = os.time()
        if now - last_upload_time < POLL_SECONDS then
            return
        end

        local content, ts, err = get_file_info(LOCAL_FILE)
        if not content then
            return -- file not ready
        end
        if ts == last_ts then
            return -- no change since last upload
        end

        print("[GitHub Uploader] File changed, starting GET request for SHA...")
        local url = "https://api.github.com/repos/" .. GITHUB_OWNER .. "/" .. GITHUB_REPO .. "/contents/" .. GITHUB_PATH
        local headers, hcount = build_github_headers()
        current_request = sapp_http.sapp_http_create_get(url, headers, hcount)
        if not current_request then
            print("[GitHub Uploader] Failed to create GET request.")
            return
        end
        state = "get_pending"
        current_content = content
        retry_count = 0
        return
    end

    if state == "get_pending" then
        if sapp_http.sapp_http_request_is_done(current_request) == 1 then
            -- Retrieve response
            local resp = ffi.new("sapp_http_response[1]")
            local ret = sapp_http.sapp_http_request_get_response(current_request, resp)
            local status = resp[0].http_status
            local body_str = ""
            if resp[0].body and resp[0].body_size > 0 then
                body_str = ffi.string(resp[0].body, resp[0].body_size)
            end

            local success = false
            local sha = nil
            if ret == 0 and (status == 200 or status == 404) then
                if status == 200 then
                    -- Extract SHA from JSON response
                    local _, pos = body_str:find('"sha":"')
                    if pos then
                        local start_pos = pos + 1
                        local end_pos = body_str:find('"', start_pos)
                        if end_pos then
                            sha = body_str:sub(start_pos, end_pos - 1)
                        end
                    end
                    success = true
                elseif status == 404 then
                    -- File doesn't exist yet, sha stays nil (will create)
                    success = true
                end
            end

            sapp_http.sapp_http_free_response(resp)

            if not success then
                local err_msg = ""
                if resp[0].error_message then
                    err_msg = ffi.string(resp[0].error_message)
                else
                    err_msg = "HTTP " .. tostring(status)
                end
                print(string_format("[GitHub Uploader] GET failed: %s", err_msg))
                retry_count = retry_count + 1
                if retry_count < MAX_RETRIES then
                    local delay = RETRY_DELAY * (2 ^ (retry_count - 1))
                    print(string_format("[GitHub Uploader] Retry GET in %d seconds", delay))
                    timer(delay * 1000, "RetryGet")
                    state = "get_retry"
                    return
                else
                    print("[GitHub Uploader] GET retries exhausted. Giving up.")
                    reset_state()
                    return
                end
            end

            -- Success: store sha and move to PUT
            current_sha = sha
            sapp_http.sapp_http_request_free(current_request)
            current_request = nil

            local encoded = base64_encode(current_content)
            local payload = '{"message":"' .. COMMIT_MSG .. '","content":"' .. encoded .. '"'
            if current_sha then
                payload = payload .. ',"sha":"' .. current_sha .. '"'
            end
            payload = payload .. "}"

            print("[GitHub Uploader] Starting PUT request...")
            local url = "https://api.github.com/repos/" .. GITHUB_OWNER
                .. "/" .. GITHUB_REPO
                .. "/contents/" .. GITHUB_PATH
            local headers, hcount = build_github_headers()
            current_request = sapp_http.sapp_http_create_put(
                url, "application/json", payload, #payload, headers, hcount
            )
            if not current_request then
                print("[GitHub Uploader] Failed to create PUT request.")
                reset_state()
                return
            end
            state = "put_pending"
            retry_count = 0
        end
        return
    end

    if state == "put_pending" then
        if sapp_http.sapp_http_request_is_done(current_request) == 1 then
            local resp = ffi.new("sapp_http_response[1]")
            local ret = sapp_http.sapp_http_request_get_response(current_request, resp)
            local status = resp[0].http_status
            local body_str = ""
            if resp[0].body and resp[0].body_size > 0 then
                body_str = ffi.string(resp[0].body, resp[0].body_size)
            end

            local success = false
            if ret == 0 and (status == 200 or status == 201) then
                success = true
            end

            sapp_http.sapp_http_free_response(resp)

            if success then
                -- Update last_ts so we don't re-upload same file
                local _, ts, _ = get_file_info(LOCAL_FILE)
                if ts then last_ts = ts end
                last_upload_time = os.time() -- record successful upload time
                print("[GitHub Uploader] Upload successful!")
                reset_state()
            else
                retry_count = retry_count + 1
                if retry_count < MAX_RETRIES then
                    local delay = RETRY_DELAY * (2 ^ (retry_count - 1))
                    print(string_format("[GitHub Uploader] PUT failed, retry in %d seconds", delay))
                    timer(delay * 1000, "RetryPut")
                    state = "put_retry"
                    return
                else
                    print("[GitHub Uploader] PUT retries exhausted. Giving up.")
                    reset_state()
                end
            end
        end
        return
    end

    -- states "get_retry" and "put_retry" are handled by timer callbacks below
end

-- Retry function for GET (called by timer)
function RetryGet()
    if state ~= "get_retry" then return end
    local url = "https://api.github.com/repos/" .. GITHUB_OWNER .. "/" .. GITHUB_REPO .. "/contents/" .. GITHUB_PATH
    local headers, hcount = build_github_headers()
    current_request = sapp_http.sapp_http_create_get(url, headers, hcount)
    if not current_request then
        print("[GitHub Uploader] Failed to create GET retry request.")
        reset_state()
        return
    end
    state = "get_pending"
end

-- Retry function for PUT (called by timer)
function RetryPut()
    if state ~= "put_retry" then return end
    local encoded = base64_encode(current_content)
    local payload = '{"message":"' .. COMMIT_MSG .. '","content":"' .. encoded .. '"'
    if current_sha then
        payload = payload .. ',"sha":"' .. current_sha .. '"'
    end
    payload = payload .. "}"

    local url = "https://api.github.com/repos/" .. GITHUB_OWNER .. "/" .. GITHUB_REPO .. "/contents/" .. GITHUB_PATH
    local headers, hcount = build_github_headers()
    current_request = sapp_http.sapp_http_create_put(url, "application/json", payload, #payload, headers, hcount)
    if not current_request then
        print("[GitHub Uploader] Failed to create PUT retry request.")
        reset_state()
        return
    end
    state = "put_pending"
end

-- Helper to get config path (SAPP's config directory)
local function getConfigPath()
    return read_string(read_dword(sig_scan('68??????008D54245468') + 0x1)) .. '\\sapp\\'
end

function OnScriptLoad()
    if sapp_http.sapp_http_global_init() ~= 0 then
        print("[GitHub Uploader] ERROR: sapp_http_global_init failed!")
        return
    end

    local version = ffi.string(sapp_http.sapp_http_version())
    print("[GitHub Uploader] Using " .. version)

    LOCAL_FILE = getConfigPath() .. LOCAL_FILE

    -- Start the fast processing loop (drives network & state machine)
    timer(FAST_INTERVAL, "ProcessLoop")

    -- Start the slow file‑check timer (once every 12 hours)
    timer(POLL_SECONDS * 1000, "CheckFileTimer")
end

function ProcessLoop()
    CheckAndUpload()
    return true -- keep looping
end

function CheckFileTimer()
    -- This is called every 12 hours; it will trigger a file check only if idle.
    CheckAndUpload()
    return true -- keep looping (will be called again in 12 hours)
end

function OnScriptUnload()
    reset_state()
    sapp_http.sapp_http_global_cleanup()
    print("[GitHub Uploader] Cleaned up.")
end
