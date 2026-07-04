# SAPP HTTP Client DLL

A lightweight HTTP/HTTPS client DLL for SAPP that exposes a C API through LuaJIT
FFI, allowing Lua scripts to perform HTTP(S) GET, POST, and PUT requests using **libcurl**.

Built with **MSVC**, **CMake**, and **vcpkg**.

[![Version][version-badge]][version-link]
[![License: MIT][license-badge]][license-link]
![C++17][cpp-badge]
![MSVC][msvc-badge]
![libcurl][curl-badge]
![Windows][windows-badge]
![CMake][cmake-badge]
![vcpkg][vcpkg-badge]

**Exported functions:**

* `sapp_http_global_init` / `sapp_http_global_cleanup`
* `sapp_http_get`
* `sapp_http_post`
* `sapp_http_put`
* `sapp_http_free_response`
* `sapp_http_version`
* `sapp_http_curl_strerror`

See [`sapp_http.h`](src/sapp_http.h) for the complete API.

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

> **If you're using VS Code with the CMake Tools extension**, you may see a false error on `find_package(CURL)` because the extension does not automatically use the vcpkg toolchain. To fix this, create a `.vscode/settings.json` file in the project root with:
>
> ```json
> {
>     "cmake.configureArgs": [
>         "-DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake",
>         "-DVCPKG_TARGET_TRIPLET=x86-windows-static"
>     ]
> }
> ```
>
> Adjust the paths to match your own vcpkg installation. After saving, run `CMake: Configure` from the command palette to refresh the configuration.
>
> **Important: As Halo is a 32-bit application, the architecture in your CMake Tools settings must match the vcpkg triplet.**  
> For the default triplet `x86-windows-static`, you need a 32-bit (Win32) build target. To enforce this:
>
> **Select the right kit** - Press `Ctrl+Shift+P`, choose `CMake: Select Kit`, and pick a kit that targets **x86** (e.g., `VS Build Tools ... - amd64_x86` or `VS Build Tools ... - x86`).  

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

## Example Lua scripts:

<details>
<summary>Click to expand</summary>

Fetches a `plain text file` from GitHub.
- [http_get_text.lua](/example_lua_scripts/http_get_text.lua)

Adds a custom `User-Agent` and `X-Custom-Header` to the request.
- [http_get_with_headers.lua](/example_lua_scripts/http_get_with_headers.lua)

Sends a `JSON` payload to an API endpoint and reads the response.
- [http_post_json.lua](/example_lua_scripts/http_post_json.lua)

Sends `application/x-www-form-urlencoded` data (like a HTML form submission).
- [http_post_form.lua](/example_lua_scripts/http_post_form.lua)

Combines a `POST` request with additional headers.
- [http_post_with_headers.lua](/example_lua_scripts/http_post_with_headers.lua)

Demonstrates updating a resource with a `PUT` request.
- [http_put.lua](/example_lua_scripts/http_put.lua)

Shows how to handle various error conditions gracefully.
- [http_error_handling.lua](/example_lua_scripts/http_http_error_handlingput.lua)

Fetches `JSON` data and demonstrates basic string manipulation to extract values.
- [http_get_json_parse.lua](/example_lua_scripts/http_get_json_parse.lua)

Sends a message to a `Discord webhook` (replace the URL with your own).
- [http_discord_webhook.lua](/example_lua_scripts/http_discord_webhook.lua)

For scripts that make multiple `HTTP` requests, you can create a reusable helper module.
- [http_helper.lua](/example_lua_scripts/http_helper.lua)
- [http_using_helper.lua](/example_lua_scripts/http_using_helper.lua)

</details>

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
