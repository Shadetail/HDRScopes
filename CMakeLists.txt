cmake_minimum_required(VERSION 3.20)
project(HDRScopes CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT so the exe is self-contained for easy launching.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- Dear ImGui (vendored, clean 1.90.6) + DX11/Win32 backends ----
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)

# ---- HDRScopes app ----
add_executable(HDRScopes WIN32
    src/main.cpp
    src/app/D3DContext.cpp
    src/util/ShaderCompiler.cpp
    src/util/Settings.cpp
    src/util/SdrWhite.cpp
    src/capture/CaptureSource.cpp
    src/capture/RegionPicker.cpp
    src/capture/PixelProbe.cpp
    src/compute/TestPattern.cpp
    src/compute/Blur.cpp
    src/scope/ScopePanel.cpp
    src/scope/ScopeFactory.cpp
    src/scope/WaveformScope.cpp
    src/scope/HistogramScope.cpp
    src/scope/ChromaScope.cpp
    src/scope/VectorScope.cpp
    src/scope/CIEScope.cpp
)
target_include_directories(HDRScopes PRIVATE src)
target_link_libraries(HDRScopes PRIVATE
    imgui
    d3d11 dxgi d3dcompiler dxguid msimg32 winmm
)
target_compile_definitions(HDRScopes PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# Locate the shaders directory at runtime relative to the source tree.
target_compile_definitions(HDRScopes PRIVATE
    HDRSCOPES_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
)
