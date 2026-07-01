set(_rhi_sources src/rhi_factory.cpp)
set(_rhi_defs "")

if (VORTEX_RHI_WITH_VULKAN)
    list(APPEND _rhi_sources
        src/vulkan/vk_device.cpp
        src/vulkan/vk_swapchain.cpp
        src/vulkan/vk_command_list.cpp
        src/vulkan/vk_surface.cpp
        src/vulkan/vk_vma.cpp)
    list(APPEND _rhi_defs VORTEX_HAVE_VULKAN)

    find_package(X11 QUIET)
    if (X11_FOUND)
        list(APPEND _rhi_defs VORTEX_HAVE_XLIB)
    endif ()
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(WaylandClient QUIET wayland-client)
        if (WaylandClient_FOUND)
            list(APPEND _rhi_defs VORTEX_HAVE_WAYLAND)
        endif ()
    endif ()
endif ()

if (VORTEX_RHI_WITH_WEBGPU)
    list(APPEND _rhi_sources
        src/webgpu/wgpu_device.cpp
        src/webgpu/wgpu_command_list.cpp
        src/webgpu/wgpu_surface.cpp)
    list(APPEND _rhi_defs VORTEX_HAVE_WEBGPU)
endif ()

vortex_add_module(rhi
    SOURCES ${_rhi_sources}
    PUBLIC_DEPS
        Vortex::core
        Vortex::platform
)

target_compile_definitions(vortex_rhi PRIVATE ${_rhi_defs})

if (NOT MSVC)
    target_compile_options(vortex_rhi PRIVATE -Wno-missing-field-initializers)
endif ()

if (VORTEX_RHI_WITH_VULKAN)
    target_link_libraries(vortex_rhi PRIVATE $<BUILD_INTERFACE:vortex_vulkan_deps>)
    if (X11_FOUND)
        target_include_directories(vortex_rhi PRIVATE ${X11_INCLUDE_DIR})
    endif ()
    if (WaylandClient_FOUND)
        target_include_directories(vortex_rhi PRIVATE ${WaylandClient_INCLUDE_DIRS})
    endif ()
    set_source_files_properties(src/vulkan/vk_vma.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif ()

if (VORTEX_RHI_WITH_WEBGPU)
    target_link_libraries(vortex_rhi PUBLIC $<BUILD_INTERFACE:vortex_webgpu_deps>)
endif ()
