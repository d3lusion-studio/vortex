if (NOT VORTEX_RHI_BACKEND STREQUAL "Vulkan")
    message(FATAL_ERROR "VORTEX_RHI_BACKEND='${VORTEX_RHI_BACKEND}' — only the Vulkan backend is implemented")
endif ()

set(_rhi_surface_defs "")
find_package(X11 QUIET)
if (X11_FOUND)
    list(APPEND _rhi_surface_defs VORTEX_HAVE_XLIB)
endif ()
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(WaylandClient QUIET wayland-client)
    if (WaylandClient_FOUND)
        list(APPEND _rhi_surface_defs VORTEX_HAVE_WAYLAND)
    endif ()
endif ()

vortex_add_module(rhi
    SOURCES
        src/vulkan/vk_device.cpp
        src/vulkan/vk_swapchain.cpp
        src/vulkan/vk_command_list.cpp
        src/vulkan/vk_surface.cpp
        src/vulkan/vk_vma.cpp
    PUBLIC_DEPS
        Vortex::core
        Vortex::platform
)

target_link_libraries(vortex_rhi PRIVATE $<BUILD_INTERFACE:vortex_vulkan_deps>)

target_compile_definitions(vortex_rhi PRIVATE ${_rhi_surface_defs})

if (NOT MSVC)
    target_compile_options(vortex_rhi PRIVATE -Wno-missing-field-initializers)
endif ()

if (X11_FOUND)
    target_include_directories(vortex_rhi PRIVATE ${X11_INCLUDE_DIR})
endif ()
if (WaylandClient_FOUND)
    target_include_directories(vortex_rhi PRIVATE ${WaylandClient_INCLUDE_DIRS})
endif ()

set_source_files_properties(src/vulkan/vk_vma.cpp PROPERTIES COMPILE_OPTIONS "-w")
