include(FetchContent)

FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        master
        GIT_SHALLOW    ON)
FetchContent_MakeAvailable(stb)
add_library(vortex_stb INTERFACE)
target_include_directories(vortex_stb INTERFACE ${stb_SOURCE_DIR})
add_library(Vortex::stb ALIAS vortex_stb)

FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.8-docking
        GIT_SHALLOW    ON)
FetchContent_MakeAvailable(imgui)
add_library(vortex_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp)
target_include_directories(vortex_imgui PUBLIC ${imgui_SOURCE_DIR})
add_library(Vortex::imgui ALIAS vortex_imgui)

FetchContent_Declare(miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG        0.11.25
        GIT_SHALLOW    ON)
FetchContent_MakeAvailable(miniaudio)
add_library(vortex_miniaudio INTERFACE)
target_include_directories(vortex_miniaudio INTERFACE ${miniaudio_SOURCE_DIR})
add_library(Vortex::miniaudio ALIAS vortex_miniaudio)

set(BOX2D_SAMPLES    OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS       OFF CACHE BOOL "" FORCE)
set(BOX2D_PROFILE    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(box2d
        GIT_REPOSITORY https://github.com/erincatto/box2d.git
        GIT_TAG        v3.1.1
        GIT_SHALLOW    ON)
FetchContent_MakeAvailable(box2d)

if (VORTEX_WITH_TRACY)
    set(TRACY_ENABLE ON  CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    FetchContent_Declare(tracy
            GIT_REPOSITORY https://github.com/wolfpld/tracy.git
            GIT_TAG        v0.11.1
            GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(tracy)
endif ()

if (VORTEX_BUILD_TESTS)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG        v2.4.11
            GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif ()

if (VORTEX_RHI_WITH_VULKAN)
    find_library(VORTEX_VULKAN_LOADER NAMES vulkan vulkan-1 REQUIRED)

    FetchContent_Declare(VulkanHeaders
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
            GIT_TAG        v1.3.296
            GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(VulkanHeaders)

    FetchContent_Declare(VulkanMemoryAllocator
            GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
            GIT_TAG        v3.1.0
            GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(VulkanMemoryAllocator)

    set(VK_BOOTSTRAP_DISABLE_WARNINGS ON CACHE BOOL "" FORCE)
    FetchContent_Declare(vk_bootstrap
            GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
            GIT_TAG        v1.3.296
            GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(vk_bootstrap)

    add_library(vortex_vulkan_deps INTERFACE)
    target_link_libraries(vortex_vulkan_deps INTERFACE
            Vulkan::Headers
            GPUOpen::VulkanMemoryAllocator
            vk-bootstrap::vk-bootstrap
            ${VORTEX_VULKAN_LOADER})
endif ()

if (VORTEX_RHI_WITH_WEBGPU)
    if (NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
             CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64"))
        message(FATAL_ERROR
            "VORTEX_RHI_WITH_WEBGPU is only wired for linux-x86_64 prebuilt wgpu-native. "
            "Add the matching archive URL/hash for ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}.")
    endif ()

    FetchContent_Declare(wgpu_native
            URL      https://github.com/gfx-rs/wgpu-native/releases/download/v0.19.4.1/wgpu-linux-x86_64-release.zip
            URL_HASH SHA256=7d73bd7af2be60b632e5ab814996acb381d1b459975d6629f91c468049c8866a)
    FetchContent_MakeAvailable(wgpu_native)

    add_library(vortex_webgpu_deps INTERFACE)
    target_include_directories(vortex_webgpu_deps INTERFACE ${wgpu_native_SOURCE_DIR})
    target_link_libraries(vortex_webgpu_deps INTERFACE
            ${wgpu_native_SOURCE_DIR}/libwgpu_native.so)
    target_link_options(vortex_webgpu_deps INTERFACE
            "-Wl,-rpath,${wgpu_native_SOURCE_DIR}")
endif ()