# Keep the Windows SDK's C language setup at file scope.
include("${CMAKE_CURRENT_LIST_DIR}/../../Tools/FindGameSproutSDL3.cmake")

function(gamesprout_use_runtime_host_input target)
    GameSproutFindSDL3(SDL3::SDL3-shared)
    set(input_platform "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../GameSproutPlayer/PlatformInput")
    target_include_directories(${target} PRIVATE "${input_platform}")
    target_link_libraries(${target} PRIVATE SDL3::SDL3-shared)
endfunction()

function(gamesprout_attach_runtime_host_input target)
    gamesprout_use_runtime_host_input(${target})
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../GameSproutPlayer/PlatformInput/SdlInputDevices.cpp")
endfunction()
