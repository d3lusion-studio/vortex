find_package(glfw3 3.4 REQUIRED CONFIG)

vortex_add_module(platform
    SOURCES
        src/glfw/glfw_window.cpp
        src/glfw/glfw_input.cpp
        src/native/native_clock.cpp
        src/native/native_filesystem.cpp
        src/native/native_dynamic_library.cpp
    PUBLIC_DEPS
        Vortex::core
    PRIVATE_DEPS
        glfw
        ${CMAKE_DL_LIBS}
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 QUIET)
    if (X11_FOUND)
        target_link_libraries(vortex_platform PRIVATE X11::X11)
    endif()

    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(WaylandClient QUIET wayland-client)
        if (WaylandClient_FOUND)
            target_link_libraries(vortex_platform PRIVATE ${WaylandClient_LIBRARIES})
            target_include_directories(vortex_platform PRIVATE ${WaylandClient_INCLUDE_DIRS})
        endif()
    endif()
elseif (WIN32)

elseif (APPLE)
    target_link_libraries(vortex_platform PRIVATE "-framework Cocoa")
endif()
