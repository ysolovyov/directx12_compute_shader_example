cmake_minimum_required(VERSION 3.17.0)

project(dx12_compute_shader_example LANGUAGES C CXX VERSION "1.0")

cmake_policy(SET CMP0063 NEW)
cmake_policy(SET CMP0135 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

if(WIN32)
    # Allow cmake to find dlls with find_library()
    list(APPEND CMAKE_FIND_LIBRARY_SUFFIXES .dll)
endif()

set(TARGET_ARCH ${CMAKE_CXX_COMPILER_ARCHITECTURE_ID})
if(TARGET_ARCH STREQUAL AMD64)
    set(TARGET_ARCH x64)
endif()

# -----------------------------------------------------------------------------
# wil - Windows-related helper types/macros
# -----------------------------------------------------------------------------
FetchContent_Declare(
    wil
    URL https://github.com/microsoft/wil/archive/refs/tags/v1.0.240803.1.zip
    URL_HASH SHA256=353D2D7F2ACEA5642689A1BA85213C1AC6816457038B54AC02158B893E0F389F
)

FetchContent_MakeAvailable(wil)

add_library(wil INTERFACE)
target_include_directories(wil INTERFACE "${wil_SOURCE_DIR}/include")

# -----------------------------------------------------------------------------
# directx-headers - to get the latest DXCore header with GENERIC_ML GUID
# -----------------------------------------------------------------------------
FetchContent_Declare(
    dxheaders
    GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers
    GIT_TAG de28d93dfa9ebf3e473127c1c657e1920a5345ee # v1.613.1
)

FetchContent_MakeAvailable(dxheaders)

add_library(dxheaders INTERFACE)
target_include_directories(dxheaders INTERFACE ${dxheaders_SOURCE_DIR}/include/directx)
target_link_libraries(dxheaders INTERFACE Microsoft::DirectX-Guids)

# compile shaders
add_custom_target(shaders)
file(GLOB SHADER "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.hlsl")

FetchContent_Declare(dxc
    URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2207/dxc_2022_07_18.zip
)
FetchContent_MakeAvailable(dxc)

foreach(FILE ${SHADER})
    get_filename_component(FILE_WE ${FILE} NAME_WE)
    add_custom_command(TARGET shaders
        COMMAND ${dxc_SOURCE_DIR}/bin/x64/dxc.exe -T cs_6_6 -O3 ${FILE} -Fo shader/${FILE_WE}.cso
        MAIN_DEPENDENCY ${FILE}
        COMMENT "HLSL ${FILE}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM)
endforeach(FILE)

add_executable(dx12_compute_shader_example main.cpp)
add_dependencies(dx12_compute_shader_example shaders)
target_precompile_headers(dx12_compute_shader_example PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
target_link_libraries(dx12_compute_shader_example wil d3d12 dxcore dxheaders d3dcompiler)

file(GLOB SHADER_CSO "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.cso")
list(LENGTH SHADER_CSO RES_LEN)
if(RES_LEN GREATER 0)
    add_custom_command(TARGET dx12_compute_shader_example PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${SHADER_CSO} $<TARGET_FILE_DIR:dx12_compute_shader_example>)
endif()