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


if (UNIX)
    add_custom_target(check_boundaries
        COMMAND bash -c
            "violations=0; \
             bad=\$(grep -rn '#include.*GLFW/glfw' ${CMAKE_SOURCE_DIR}/engine \
                   --include='*.hpp' --include='*.cpp' \
                   | grep -v 'platform/src/glfw/'); \
             if [ -n \"\$bad\" ]; then \
                 echo 'BOUNDARY VIOLATION — GLFW leaked outside platform/src/glfw/:'; \
                 echo \"\$bad\"; violations=1; fi; \
             bad=\$(grep -rnE '#include.*(vulkan|vk_mem_alloc|VkBootstrap)' ${CMAKE_SOURCE_DIR}/engine \
                   --include='*.hpp' --include='*.cpp' \
                   | grep -v 'rhi/src/vulkan/'); \
             if [ -n \"\$bad\" ]; then \
                 echo 'BOUNDARY VIOLATION — Vulkan leaked outside rhi/src/vulkan/:'; \
                 echo \"\$bad\"; violations=1; fi; \
             bad=\$(grep -rnE '#include.*miniaudio' ${CMAKE_SOURCE_DIR}/engine \
                   --include='*.hpp' --include='*.cpp' \
                   | grep -v 'audio/src/miniaudio/'); \
             if [ -n \"\$bad\" ]; then \
                 echo 'BOUNDARY VIOLATION — miniaudio leaked outside audio/src/miniaudio/:'; \
                 echo \"\$bad\"; violations=1; fi; \
             bad=\$(grep -rnE '#include.*box2d' ${CMAKE_SOURCE_DIR}/engine \
                   --include='*.hpp' --include='*.cpp' \
                   | grep -v 'physics/src/box2d/'); \
             if [ -n \"\$bad\" ]; then \
                 echo 'BOUNDARY VIOLATION — Box2D leaked outside physics/src/box2d/:'; \
                 echo \"\$bad\"; violations=1; fi; \
             if [ \$violations -eq 0 ]; then \
                 echo 'OK: abstraction boundaries are clean.'; fi; \
             exit \$violations"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking abstraction boundary violations..."
        VERBATIM
    )
endif()
