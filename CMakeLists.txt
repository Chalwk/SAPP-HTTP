cmake_minimum_required(VERSION 3.21)

project(sapp_http VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(NOMINMAX)
add_library(sapp_http SHARED
    src/sapp_http.cpp
    sapp_http.def
)

target_compile_definitions(sapp_http PRIVATE NOMINMAX)

target_include_directories(sapp_http
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(sapp_http PRIVATE cxx_std_17)

target_compile_definitions(sapp_http PRIVATE
    SAPPHTTP_BUILD
    CURL_STATICLIB
)

if (MSVC)
    target_compile_options(sapp_http PRIVATE /W4 /permissive- /EHsc)
endif()

find_package(CURL CONFIG REQUIRED)

target_link_libraries(sapp_http PRIVATE
    CURL::libcurl
    ws2_32
    crypt32
    bcrypt
    normaliz
    winmm
)

set_target_properties(sapp_http PROPERTIES
    OUTPUT_NAME "sapp_http"
)

install(TARGETS sapp_http RUNTIME DESTINATION bin)
install(FILES include/sapp_http.h DESTINATION include)