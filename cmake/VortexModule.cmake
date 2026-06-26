function(vortex_add_module name)
    set(multi SOURCES PUBLIC_DEPS PRIVATE_DEPS)
    cmake_parse_arguments(M "" "" "${multi}" ${ARGN})

    if(M_SOURCES)
        add_library(vortex_${name} STATIC ${M_SOURCES})
        set(scope_pub PUBLIC)
        set(scope_priv PRIVATE)
    else()
        add_library(vortex_${name} INTERFACE)
        set(scope_pub INTERFACE)
        set(scope_priv INTERFACE)
    endif()

    add_library(Vortex::${name} ALIAS vortex_${name})
    set_target_properties(vortex_${name} PROPERTIES EXPORT_NAME ${name})

    target_include_directories(vortex_${name} ${scope_pub}
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    if(M_SOURCES)
        target_include_directories(vortex_${name} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src)
    endif()

    target_compile_features(vortex_${name} ${scope_pub} cxx_std_20)
    target_link_libraries(vortex_${name}
            ${scope_pub}  ${M_PUBLIC_DEPS}
            ${scope_priv} ${M_PRIVATE_DEPS})

    if(VORTEX_WARNINGS_AS_ERRORS AND M_SOURCES)
        if(MSVC)
            target_compile_options(vortex_${name} PRIVATE /W4 /WX)
        else()
            target_compile_options(vortex_${name} PRIVATE -Wall -Wextra -Werror)
        endif()
    endif()

    if(VORTEX_INSTALL)
        install(TARGETS vortex_${name}
                EXPORT VortexTargets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/include)
            install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
        endif()
    endif()
endfunction()
