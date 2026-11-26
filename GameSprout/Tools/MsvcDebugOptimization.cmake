include_guard(GLOBAL)

option(GAMESPROUT_OPTIMIZE_MSVC_DEBUG "Optimize GameSprout gameplay targets in MSVC Debug" ON)

function(GameSproutOptimizeMsvcDebugFlags flags output)
    if(GAMESPROUT_OPTIMIZE_MSVC_DEBUG AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(flags " ${flags} ")
        # MSVC rejects runtime checks with optimization. Keep debug symbols
        # and the configuration's CRT selection while replacing those flags.
        foreach(setting IN ITEMS "RTC[sSuU1]+" "GZ" "Od" "Ob0")
            string(REGEX REPLACE "[ \t][/-]${setting}[ \t]" " " flags "${flags}")
        endforeach()
        if(NOT flags MATCHES "[ \t][/-]O[12x][ \t]")
            string(PREPEND flags "/O2")
        endif()
        if(NOT flags MATCHES "[ \t][/-]Ob[123][ \t]")
            string(APPEND flags "/Ob2")
        endif()
        string(STRIP "${flags}" flags)
    endif()
    set(${output} "${flags}" PARENT_SCOPE)
endfunction()
