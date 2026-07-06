# SAPP HTTP Client DLL (Asynchronous Only)

A lightweight, HTTP/HTTPS client DLL for SAPP that exposes a C API through LuaJIT FFI, allowing Lua scripts to perform HTTP(S) GET, POST, and PUT requests using **libcurl**.

All requests are **asynchronous** - they never block the main thread. You must periodically call `sapp_http_process()` to drive the transfers and check completion with `sapp_http_request_is_done()`.

[![Version][version-badge]][version-link]
[![License: MIT][license-badge]][license-link]
![C++17][cpp-badge]
![MSVC][msvc-badge]
![libcurl][curl-badge]
![Windows][windows-badge]
![CMake][cmake-badge]
![vcpkg][vcpkg-badge]

**Exported functions:**

* **Global** - `sapp_http_global_init`, `sapp_http_global_cleanup`
* **Asynchronous** - `sapp_http_create_get`, `sapp_http_create_post`, `sapp_http_create_put`, `sapp_http_process`, `sapp_http_request_is_done`, `sapp_http_request_get_response`, `sapp_http_request_free`
* **Utilities** - `sapp_http_free_response`, `sapp_http_version`, `sapp_http_curl_strerror`

See [`sapp_http.h`](src/sapp_http.h) for the complete API.

---

## Requirements

* **Visual Studio Build Tools 2022** (or later) with the **Desktop development with C++** workload, including:
  * MSVC v143
  * Windows 10/11 SDK
  * CMake tools
* **vcpkg**
* **CMake** 3.21 or later

---

## Building the DLL

### 1. Install vcpkg

Open a terminal and clone vcpkg into a convenient location, e.g. `C:\dev\vcpkg`:

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

### 4. Configure with CMake

```cmd
cmake -B build -A Win32 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x86-windows-static
```

### 5. Build the DLL

```cmd
cmake --build build --config Release
```

On success, the DLL is created at: `C:\dev\sapp-http\build\Release\sapp_http.dll`

Alternatively, run the convenience script: `build.bat`

---

## Deploying

Copy `sapp_http.dll` into the same folder as `sapp.dll` (the SAPP server binary).  
Then, from your Lua scripts, use `ffi.load("sapp_http")` and call the API.

---

## Example Lua Script (Async GET)

```lua
local ffi = require("ffi")
local http = ffi.load("sapp_http")

-- Load the header definitions (see sapp_http.h)
ffi.cdef[[
    typedef struct { const char *name; const char *value; } sapp_http_header;
    typedef struct { int curl_code; long http_status; size_t body_size; char *body; char *content_type; char *error_message; } sapp_http_response;
    typedef struct sapp_http_request sapp_http_request;
    int sapp_http_global_init(void);
    sapp_http_request* sapp_http_create_get(const char *url, const sapp_http_header *headers, size_t header_count);
    int sapp_http_process(void);
    int sapp_http_request_is_done(sapp_http_request *req);
    int sapp_http_request_get_response(sapp_http_request *req, sapp_http_response *out);
    void sapp_http_request_free(sapp_http_request *req);
    void sapp_http_free_response(sapp_http_response *resp);
    void sapp_http_global_cleanup(void);
]]

-- Initialise once at startup
http.sapp_http_global_init()

-- Create a GET request
local req = http.sapp_http_create_get("https://httpbin.org/get", nil, 0)
if req == nil then
    print("Failed to create request")
    os.exit(1)
end

-- Poll until complete
while http.sapp_http_request_is_done(req) == 0 do
    http.sapp_http_process()
    -- Yield to other Lua tasks if needed (e.g. coroutine.yield())
end

-- Retrieve the response
local resp = ffi.new("sapp_http_response")
local status = http.sapp_http_request_get_response(req, resp)
if status == 0 then
    print("Status: " .. resp.http_status)
    print("Body: " .. ffi.string(resp.body, resp.body_size))
    http.sapp_http_free_response(resp)
else
    print("Error: " .. status)
end

-- Clean up
http.sapp_http_request_free(req)
http.sapp_http_global_cleanup()
```

For more advanced, SAPP-specific examples (custom headers, POST/PUT, JSON handling, error handling, etc.), see the [`example_lua_scripts`](/example_lua_scripts) folder.

---

## License

This project is provided under the [MIT licence](LICENSE).  
libcurl is distributed under the [curl licence](https://curl.se/docs/copyright.html).

---

[version-badge]: https://img.shields.io/github/v/release/Chalwk/SAPP-HTTP?display_name=tag
[version-link]: https://github.com/Chalwk/SAPP-HTTP/releases/latest
[license-badge]: https://img.shields.io/github/license/Chalwk/SAPP-HTTP
[license-link]: https://github.com/Chalwk/SAPP-HTTP/blob/main/LICENSE
[cpp-badge]: https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white
[msvc-badge]: https://img.shields.io/badge/MSVC-2022-5C2D91?logo=visualstudio&logoColor=white
[curl-badge]: https://img.shields.io/badge/libcurl-8.x-073551?logo=curl&logoColor=white
[windows-badge]: https://img.shields.io/badge/Windows-10%2F11-0078D6?logo=windows&logoColor=white
[cmake-badge]: https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white
[vcpkg-badge]: https://img.shields.io/badge/vcpkg-Package%20Manager-0C7BDC
