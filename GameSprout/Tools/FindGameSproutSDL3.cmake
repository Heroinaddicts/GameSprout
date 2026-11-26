# Qt Creator provides a C compiler in the cache even for CXX-only projects.
# The Windows SDL3 SDK uses it to check the target architecture.
if(WIN32)
    enable_language(C)
endif()

include_guard(GLOBAL)

function(GameSproutFindSDL3 target)
    if(TARGET "${target}")
        return()
    endif()

    set(sdl_hints "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../SDL/build")
    if(WIN32)
        file(GLOB windows_sdl_hints LIST_DIRECTORIES true
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../Bin/Deps/SDL3Windows/*/cmake")
        list(SORT windows_sdl_hints COMPARE NATURAL ORDER DESCENDING)
        list(PREPEND sdl_hints ${windows_sdl_hints})
    endif()
    string(REPLACE "SDL3::" "" sdl_component "${target}")
    find_package(SDL3 3.2 CONFIG REQUIRED COMPONENTS "${sdl_component}" HINTS ${sdl_hints})
endfunction()
