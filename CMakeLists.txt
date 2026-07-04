cmake_minimum_required(VERSION 3.21)

project(sapp_http VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(NOMINMAX)

add_library(${PROJECT_NAME} SHARED
    src/sapp_http.cpp
    src/sapp_http.def
)

target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${PROJECT_NAME} PRIVATE SAPPHTTP_BUILD CURL_STATICLIB)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /EHsc)
endif()

find_package(CURL CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CURL::libcurl
    ws2_32
    crypt32
    bcrypt
    normaliz
    winmm
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "sapp_http")

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(FILES src/sapp_http.h DESTINATION include)