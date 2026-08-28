# webgpu.cmake — pinned wgpu-native (WebGPU) prebuilt integration.
#
# Gated behind MGB64_WEBGPU_BACKEND (OFF by default) so the shipping GL/Metal
# build is completely unaffected until we deliberately flip the default.
#
# wgpu-native is the gfx-rs C implementation of the standard `webgpu.h` C API. We
# consume its per-platform PREBUILT release (no Rust toolchain needed), downloaded
# and SHA-256-verified at configure time and pinned to an exact version — matching
# the repo's pinned-dependency doctrine (MinGW SDL2, appimagetool). Prebuilts exist
# for macOS (arm64/x86_64), Windows (x86_64-gnu, for the MinGW build), and Linux
# (x86_64 AND aarch64 — the PortMaster handheld). The `webgpu.h` API is identical
# whether backed by wgpu-native or Dawn, so the backend code stays portable.
#
# Exposes: INTERFACE target `webgpu` (include dir + static lib + system deps).

# --- Emscripten: browser WebGPU via the emdawnwebgpu port; no native prebuilt ---
# The FATAL_ERROR "unsupported platform" below must never be reached under
# Emscripten, and no wgpu-native prebuilt exists for wasm — so short-circuit
# here with an INTERFACE target backed by the port instead.
if(EMSCRIPTEN)
    add_library(webgpu INTERFACE)
    # emdawnwebgpu supplies the modern unified webgpu.h (same webgpu-headers
    # lineage as wgpu-native v29) implemented over the browser's WebGPU API.
    target_compile_options(webgpu INTERFACE "--use-port=emdawnwebgpu")
    target_link_options(webgpu INTERFACE "--use-port=emdawnwebgpu")
    return()
endif()

if(NOT MGB64_WEBGPU_BACKEND)
    return()
endif()

set(WGPU_NATIVE_VERSION "v29.0.1.1")
set(_wgpu_base "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_NATIVE_VERSION}")

# Select the prebuilt for this platform + arch, with its pinned SHA-256 (from the
# release's published digests).
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_wgpu_asset "wgpu-macos-aarch64-release.zip")
        set(_wgpu_sha  "a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e")
    else()
        set(_wgpu_asset "wgpu-macos-x86_64-release.zip")
        set(_wgpu_sha  "8e2f7378548ddd0e2cf21e7d864dda46e953f0af724855a33778b85ead206d41")
    endif()
elseif(WIN32)
    # MinGW/GNU ABI to match the repo's x86_64-w64-mingw32 cross-build.
    set(_wgpu_asset "wgpu-windows-x86_64-gnu-release.zip")
    set(_wgpu_sha  "d471e3614733c1d4ddd61bfd19868356477d0d37bf531bf8c6cb64a7f579bd2a")
elseif(UNIX)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_wgpu_asset "wgpu-linux-aarch64-release.zip")   # PortMaster/ARM handheld
        set(_wgpu_sha  "015fcdf1dbae82e614a783cc38017e5399ae0927a889fe9b69c9b664bc61b47a")
    else()
        set(_wgpu_asset "wgpu-linux-x86_64-release.zip")
        set(_wgpu_sha  "95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a")
    endif()
else()
    message(FATAL_ERROR "MGB64_WEBGPU_BACKEND: unsupported platform for wgpu-native prebuilt")
endif()

include(FetchContent)
FetchContent_Declare(wgpu_native
    URL "${_wgpu_base}/${_wgpu_asset}"
    URL_HASH SHA256=${_wgpu_sha}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(wgpu_native)
# wgpu_native_SOURCE_DIR now contains include/webgpu/{webgpu,wgpu}.h + lib/.

add_library(webgpu INTERFACE)
target_include_directories(webgpu INTERFACE "${wgpu_native_SOURCE_DIR}/include")

if(WIN32)
    target_link_libraries(webgpu INTERFACE
        "${wgpu_native_SOURCE_DIR}/lib/libwgpu_native.a"
        d3dcompiler ws2_32 userenv bcrypt ntdll opengl32 ole32 oleaut32 propsys runtimeobject)
else()
    target_link_libraries(webgpu INTERFACE "${wgpu_native_SOURCE_DIR}/lib/libwgpu_native.a")
endif()

if(APPLE)
    # System frameworks the wgpu-native static lib references (Metal backend + the
    # Rust std runtime).
    target_link_libraries(webgpu INTERFACE
        "-framework Metal" "-framework QuartzCore" "-framework Foundation"
        "-framework CoreFoundation" "-framework IOKit" "-framework IOSurface"
        "-framework CoreGraphics" "-framework AppKit" "-framework Security")
elseif(UNIX)
    target_link_libraries(webgpu INTERFACE m dl pthread)
endif()

message(STATUS "WebGPU backend: wgpu-native ${WGPU_NATIVE_VERSION} (${_wgpu_asset})")
