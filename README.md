# SAPP HTTP Client DLL

A lightweight HTTP/HTTPS client DLL for SAPP (Halo Custom Edition server extension) that exposes a C API through LuaJIT
FFI, allowing Lua scripts to perform HTTP(S) GET, POST, and PUT requests using **libcurl**.

Built with **MSVC**, **CMake**, and **vcpkg**.

**Exported functions:**

* `sapp_http_global_init` / `sapp_http_global_cleanup`
* `sapp_http_get`
* `sapp_http_post`
* `sapp_http_put`
* `sapp_http_free_response`
* `sapp_http_version`
* `sapp_http_curl_strerror`

See [`sapp_http.h`](include/sapp_http.h) for the complete API.

---

## Requirements

* **Visual Studio Build Tools 2022** (or later) with the **Desktop development with C++** workload
    * MSVC v143
    * Windows 10/11 SDK
    * CMake tools
    * vcpkg
* **CMake** 3.21 or later

---

## Building the DLL

### 1. Install vcpkg

Open a terminal and clone vcpkg into a convenient location (here `C:\dev\vcpkg`):

```cmd
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
cd C:\dev\vcpkg
bootstrap-vcpkg.bat
```

### 2. Install libcurl (static 32-bit)

```cmd
C:\dev\vcpkg\vcpkg.exe install curl:x86-windows-static
```

### 3. Get the source code

Clone this repository (or place the sources in a folder, e.g. `C:\dev\sapp-http`):

```cmd
git clone https://github.com/Chalwk/SAPP-HTTP.git C:\dev\sapp-http
cd C:\dev\sapp-http
```

Ensure the following files exist:

```
C:\dev\sapp-http\
  CMakeLists.txt
  sapp_http.def
  include\sapp_http.h
  src\sapp_http.cpp
```

### 4. Configure with CMake

Open the **x86 Native Tools Command Prompt for VS** (or any terminal where `cmake` is available) and run:

```cmd
cmake -S C:\dev\sapp-http -B build -A Win32 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x86-windows-static
```

This generates 32-bit Visual Studio project files and locates the libcurl dependencies via vcpkg.

### 5. Build the DLL

```cmd
cmake --build build --config Release
```

On success, the DLL is created at:

```
C:\dev\sapp-http\build\Release\sapp_http.dll
```

---

## Verifying the build

To confirm that all symbols are exported correctly:

```cmd
dumpbin /exports build\Release\sapp_http.dll
```

You should see the list of exported functions.

---

## Deploying

Copy `sapp_http.dll` into the same folder as `sapp.dll` (the SAPP server binary).  
Then, from your Lua scripts, use `ffi.load("sapp_http")` and call the API.

---

## Example Lua script:

```lua
local ffi = require("ffi")

-- Load the DLL (adjust path if needed)
local http = ffi.load("sapp_http")

-- C declarations
ffi.cdef[[
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

enum sapp_http_status {
    SAPPHTTP_OK = 0,
    SAPPHTTP_E_INVALID_ARGUMENT = -1,
    SAPPHTTP_E_CURL_INIT_FAILED = -2,
    SAPPHTTP_E_CURL_OPTION_FAILED = -3,
    SAPPHTTP_E_OUT_OF_MEMORY = -4
};

int sapp_http_global_init(void);
void sapp_http_global_cleanup(void);

int sapp_http_get(const char *url,
                  const sapp_http_header *headers,
                  size_t header_count,
                  sapp_http_response *out_response);

int sapp_http_post(const char *url,
                   const char *content_type,
                   const char *body,
                   size_t body_size,
                   const sapp_http_header *headers,
                   size_t header_count,
                   sapp_http_response *out_response);

int sapp_http_put(const char *url,
                  const char *content_type,
                  const char *body,
                  size_t body_size,
                  const sapp_http_header *headers,
                  size_t header_count,
                  sapp_http_response *out_response);

void sapp_http_free_response(sapp_http_response *response);

const char *sapp_http_version(void);
const char *sapp_http_curl_strerror(int curl_code);
]]

local function print_response(resp)
    print("  curl_code   : " .. resp.curl_code)
    print("  http_status : " .. resp.http_status)
    print("  body_size   : " .. resp.body_size)
    if resp.body ~= nil then
        print("  body        : " .. ffi.string(resp.body, resp.body_size))
    else
        print("  body        : (null)")
    end
    print("  content_type: " .. (resp.content_type or "(null)"))
    print("  error_msg   : " .. (resp.error_message or "(none)"))
end

local function main()
    -- 1. Initialize libcurl globally
    local init_ret = http.sapp_http_global_init()
    if init_ret ~= 0 then
        print("Global init failed with code " .. init_ret)
        return
    end

    -- Print libcurl version
    print("libcurl version: " .. http.sapp_http_version())

    -- 2. Prepare a GET request with custom headers
    local headers = ffi.new("sapp_http_header[2]", {
        { name = "X-Custom-Header", value = "HelloFromLua" },
        { name = "Accept", value = "application/json" }
    })
    local resp = ffi.new("sapp_http_response")

    print("\n--- GET request ---")
    local ret = http.sapp_http_get("https://httpbin.org/get", headers, 2, resp)
    print("sapp_http_get returned: " .. ret)
    print_response(resp)
    http.sapp_http_free_response(resp)

    -- 3. POST request with JSON body
    local json_body = [[{"name":"Lua","version":"5.1"}]]
    local resp2 = ffi.new("sapp_http_response")
    print("\n--- POST request ---")
    ret = http.sapp_http_post("https://httpbin.org/post",
                              "application/json",
                              json_body,
                              #json_body,
                              nil, 0, resp2)
    print("sapp_http_post returned: " .. ret)
    print_response(resp2)
    http.sapp_http_free_response(resp2)

    -- 4. PUT request (similar)
    local resp3 = ffi.new("sapp_http_response")
    print("\n--- PUT request ---")
    ret = http.sapp_http_put("https://httpbin.org/put",
                             "application/json",
                             json_body,
                             #json_body,
                             nil, 0, resp3)
    print("sapp_http_put returned: " .. ret)
    print_response(resp3)
    http.sapp_http_free_response(resp3)

    -- 5. Show error string for a known code
    local dummy_code = 6 -- CURLE_COULDNT_RESOLVE_HOST
    print("\nError string for code " .. dummy_code .. ": " ..
          http.sapp_http_curl_strerror(dummy_code))

    -- 6. Clean up
    http.sapp_http_global_cleanup()
    print("\nDone.")
end

-- Run the example (pcall to catch any FFI errors)
local ok, err = pcall(main)
if not ok then
    print("Error: " .. tostring(err))
end
```

---

## License

This project is provided under the [MIT licence](LICENSE).  
libcurl is distributed under the [curl licence](https://curl.se/docs/copyright.html).