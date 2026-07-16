cmake_minimum_required(VERSION 3.18)

project(
        Vortex
        VERSION 0.1.0
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif ()

include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(VortexModule)
include(VortexEmbed)

option(VORTEX_BUILD_EXAMPLES "Build Vortex example" ${PROJECT_IS_TOP_LEVEL})
option(VORTEX_BUILD_GAMES "Build the games built on Vortex" ${PROJECT_IS_TOP_LEVEL})
option(VORTEX_BUILD_TOOLS "Build Vortex tools (asset cooker, ...)" ${PROJECT_IS_TOP_LEVEL})
option(VORTEX_BUILD_TESTS "Build Vortex test" ${PROJECT_IS_TOP_LEVEL})
option(VORTEX_INSTALL "Generate install/export rules" ${PROJECT_IS_TOP_LEVEL})
option(VORTEX_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(VORTEX_WITH_TRACY "Enable the Tracy profiler backend" OFF)

set(VORTEX_RHI_BACKEND "Vulkan" CACHE STRING "Default graphics backend (used by examples when VORTEX_RHI_API is unset)")
set_property(CACHE VORTEX_RHI_BACKEND PROPERTY STRINGS Vulkan WebGPU)

option(VORTEX_RHI_WITH_VULKAN "Build the Vulkan RHI backend" ON)
option(VORTEX_RHI_WITH_WEBGPU "Build the WebGPU (wgpu-native) RHI backend" ON)

if (NOT VORTEX_RHI_WITH_VULKAN AND NOT VORTEX_RHI_WITH_WEBGPU)
    message(FATAL_ERROR "At least one RHI backend must be enabled")
endif ()

add_subdirectory(third_party)
add_subdirectory(engine)

if(VORTEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(VORTEX_BUILD_GAMES)
    add_subdirectory(games)
endif()

if(VORTEX_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

#if(VORTEX_BUILD_TESTS)
#    enable_testing()
#    add_subdirectory(tests)
#endif()

target_include_directories(vortex_core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(VORTEX_INSTALL)
    include(CMakePackageConfigHelpers)
    set(VORTEX_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/Vortex)

    install(EXPORT VortexTargets
            NAMESPACE Vortex::
            FILE VortexTargets.cmake
            DESTINATION ${VORTEX_CMAKE_DIR})

    configure_package_config_file(
            cmake/VortexConfig.cmake.in
            ${CMAKE_CURRENT_BINARY_DIR}/VortexConfig.cmake
            INSTALL_DESTINATION ${VORTEX_CMAKE_DIR})

    write_basic_package_version_file(
            ${CMAKE_CURRENT_BINARY_DIR}/VortexConfigVersion.cmake
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion)

    install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/VortexConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/VortexConfigVersion.cmake
            DESTINATION ${VORTEX_CMAKE_DIR})
endif()