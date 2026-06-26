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

option(VORTEX_BUILD_EXAMPLES "Build Vortex example" ${PROJECT_IS_TOP_LEVEL})
option(VORTEX_BUILD_TESTS "Build Vortex test" ${PROJECT_IS_TOP_LEVEL})
option(VORTEX_INSTALL "Generate install/export rules" ${PROJECT_IS_TOP_LEVEL})
option(VORTEX_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

set(VORTEX_RHI_BACKEND "Vulkan" CACHE STRING "Graphics Backend")
set_property(CACHE VORTEX_RHI_BACKEND PROPERTY STRINGS Vulkan DX12 Metal WebGPU)

add_subdirectory(third_party)
add_subdirectory(engine)

if(VORTEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
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