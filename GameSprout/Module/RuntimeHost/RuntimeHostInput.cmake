function(gamesprout_use_runtime_host_input target)
    if(NOT TARGET SDL3::SDL3-shared)
        find_package(SDL3 3.2 CONFIG REQUIRED COMPONENTS SDL3-shared
            HINTS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../../SDL/build")
    endif()
    set(input_platform "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../GameSproutPlayer/PlatformInput")
    target_include_directories(${target} PRIVATE "${input_platform}")
    target_link_libraries(${target} PRIVATE SDL3::SDL3-shared)
endfunction()

function(gamesprout_attach_runtime_host_input target)
    gamesprout_use_runtime_host_input(${target})
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../GameSproutPlayer/PlatformInput/SdlInputDevices.cpp")
endfunction()
