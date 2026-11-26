include_guard(GLOBAL)

function(GameSproutDeployQtWindows target)
    if(NOT WIN32 OR NOT TARGET Qt6::windeployqt)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "$<TARGET_FILE:Qt6::windeployqt>"
            --no-compiler-runtime "$<TARGET_FILE:${target}>"
        COMMENT "Deploying Qt runtime and platform plugins for ${target}"
        VERBATIM)
endfunction()
