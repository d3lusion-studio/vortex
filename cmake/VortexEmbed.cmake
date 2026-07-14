set(VORTEX_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/EmbedAssets.cmake" CACHE INTERNAL "")

# Compile files INTO a target, loadable through the normal asset API.
#
#   vortex_embed_asset(my_game
#       NAMESPACE game
#       FILES assets/icon.png assets/theme.pal)
#
#   #include "embedded_game.hpp"
#   vortexEmbed_game(mgr);                          // once, at startup
#   mgr.load<TextureAsset>("embedded://icon.png");  // from then on it is just an asset
#
# NAMES lets a file be embedded under a different name than its own:
#   vortex_embed_asset(t NAMESPACE g FILES a/b/logo.png NAMES branding.png)
#
# The generated source is a build artifact and is regenerated whenever a listed file changes — so
# an embedded asset is still editable during development, it just costs a rebuild instead of a
# hot reload. That trade is the honest one: bytes baked into a binary genuinely cannot change
# without rebuilding the binary.
function(vortex_embed_asset target)
    set(one   NAMESPACE)
    set(multi FILES NAMES)
    cmake_parse_arguments(E "" "${one}" "${multi}" ${ARGN})

    if(NOT E_FILES)
        message(FATAL_ERROR "vortex_embed_asset(${target}): FILES is required")
    endif()
    if(NOT E_NAMESPACE)
        set(E_NAMESPACE ${target})
    endif()

    # Absolute paths, so the generator can be run from anywhere; and default each asset's name to
    # its bare filename, which is what a game would naturally write in load().
    set(abs_files "")
    set(names "")
    set(i 0)
    foreach(f ${E_FILES})
        get_filename_component(abs "${f}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND abs_files "${abs}")

        list(LENGTH E_NAMES name_count)
        if(i LESS name_count)
            list(GET E_NAMES ${i} n)
        else()
            get_filename_component(n "${f}" NAME)
        endif()
        list(APPEND names "embedded://${n}")

        math(EXPR i "${i} + 1")
    endforeach()

    set(gen_src "${CMAKE_CURRENT_BINARY_DIR}/embedded_${E_NAMESPACE}.cpp")
    set(gen_hdr "${CMAKE_CURRENT_BINARY_DIR}/embedded_${E_NAMESPACE}.hpp")

    add_custom_command(
        OUTPUT  "${gen_src}" "${gen_hdr}"
        COMMAND ${CMAKE_COMMAND}
                -DOUT=${gen_src} -DHDR=${gen_hdr} -DSUFFIX=${E_NAMESPACE}
                "-DFILES=${abs_files}" "-DNAMES=${names}"
                -P "${VORTEX_EMBED_SCRIPT}"
        DEPENDS ${abs_files} "${VORTEX_EMBED_SCRIPT}"
        COMMENT "Embedding assets into ${target}"
        VERBATIM)

    target_sources(${target} PRIVATE "${gen_src}")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()
