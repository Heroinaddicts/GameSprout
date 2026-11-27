include_guard(GLOBAL)

option(GAMESPROUT_ENABLE_TRACY "Enable Tracy instrumentation for desktop GameSprout targets" OFF)

function(GameSproutRequireTracyClient)
    if(TARGET Tracy::TracyClient)
        get_target_property(gamesprout_client Tracy::TracyClient GAMESPROUT_TRACY_CLIENT)
        if(NOT gamesprout_client)
            message(FATAL_ERROR "GAMESPROUT_ENABLE_TRACY requires GameSprout's pinned Tracy client, but Tracy::TracyClient already exists.")
        endif()
        return()
    endif()
    if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Windows|Darwin|Linux)$" OR
       GAMESPROUT_GRHI_STATIC OR GAMESPROUT_RUNTIME_STATIC)
        message(FATAL_ERROR "GAMESPROUT_ENABLE_TRACY requires a desktop build with shared GRHI and Runtime.")
    endif()

    # These settings are local to the pinned client and leave the engine's build options alone.
    set(TRACY_STATIC OFF)
    set(TRACY_LTO OFF)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
    set(CMAKE_DISABLE_FIND_PACKAGE_rocprofiler-sdk TRUE)
    set(TRACY_Fortran OFF)
    set(TRACY_ENABLE ON)
    set(TRACY_ON_DEMAND ON)
    set(TRACY_ONLY_LOCALHOST ON)
    set(TRACY_NO_BROADCAST ON)
    set(TRACY_NO_SYSTEM_TRACING ON)
    set(TRACY_NO_SAMPLING ON)
    set(TRACY_NO_CRASH_HANDLER ON)

    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    # Add the source ourselves so the upstream standalone install rules stay out of the SDK.
    FetchContent_Declare(gamesprout_tracy
        URL "https://codeload.github.com/wolfpld/tracy/tar.gz/refs/tags/v0.15.0"
        DOWNLOAD_NAME "tracy-0.15.0.tar.gz"
        URL_HASH "SHA256=3d57da54aa67d7875da1e8fecd0bd5957675cef73c27dd52bffb6ab8d290a5e6"
        SOURCE_SUBDIR cmake)
    FetchContent_MakeAvailable(gamesprout_tracy)
    add_subdirectory("${gamesprout_tracy_SOURCE_DIR}" "${gamesprout_tracy_BINARY_DIR}" EXCLUDE_FROM_ALL)

    target_sources(TracyClient PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/FrameTimingTracy.cpp")
    target_compile_features(TracyClient PRIVATE cxx_std_20)
    configure_file("${gamesprout_tracy_SOURCE_DIR}/LICENSE"
        "${gamesprout_tracy_BINARY_DIR}/Tracy-LICENSE.txt" COPYONLY)
    set_property(TARGET TracyClient PROPERTY GAMESPROUT_TRACY_LICENSE
        "${gamesprout_tracy_BINARY_DIR}/Tracy-LICENSE.txt")
    # Keep one loader name across the SDK, editor plugins and Player packages.
    set_property(TARGET TracyClient PROPERTY VERSION)
    set_target_properties(TracyClient PROPERTIES
        GAMESPROUT_TRACY_CLIENT TRUE
        DEBUG_POSTFIX ""
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF)
    if(WIN32)
        set_target_properties(TracyClient PROPERTIES PREFIX "")
    endif()
endfunction()

function(GameSproutEnableTracy target)
    if(GAMESPROUT_ENABLE_TRACY)
        GameSproutRequireTracyClient()
        target_link_libraries(${target} PRIVATE Tracy::TracyClient)
        target_compile_definitions(${target} PRIVATE GAMESPROUT_TRACY_ENABLED=1)
        if(ARGC GREATER 1 AND UNIX)
            if(APPLE)
                set(loader_path "@loader_path")
            else()
                set(loader_path "$ORIGIN")
            endif()
            set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "${loader_path}/${ARGV1}")
            set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "${loader_path}/${ARGV1}")
        endif()
    endif()
endfunction()

function(GameSproutStageTracy target)
    if(GAMESPROUT_ENABLE_TRACY)
        get_target_property(tracy_license Tracy::TracyClient GAMESPROUT_TRACY_LICENSE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:Tracy::TracyClient>" "${tracy_license}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()
