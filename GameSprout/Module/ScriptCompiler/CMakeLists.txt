cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")

# Querying the launcher's output directory must not make the plugin depend
# on the launcher; the launcher already depends on its plugins.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ScriptCompiler VERSION 0.1 LANGUAGES CXX)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../Foundation/FoundationApi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutRuntime/GameSproutRuntime.cmake")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

# Keep the target and IDE tree in sync when files are added or removed.
file(GLOB_RECURSE module_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
)
# Exclude build output, separately built dependencies and test entry points.
list(FILTER module_files EXCLUDE REGEX "/(build|cmake-build-[^/]+|CMakeFiles|[^/]+_autogen|ThirdParty|Tests|Tools)/")
list(APPEND module_files "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")

add_library(ScriptCompiler SHARED ${module_files})
target_compile_features(ScriptCompiler PRIVATE cxx_std_20)
target_link_libraries(ScriptCompiler PRIVATE
    Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntimeApi
    Qt${QT_VERSION_MAJOR}::Core
)
target_include_directories(ScriptCompiler PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../View/Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutEditor/Api"
)
set_target_properties(ScriptCompiler PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${module_files}
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(GLOB_RECURSE rhi_adapater_interface_files CONFIGURE_DEPENDS
        LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/../Interface/*"
    )
    target_sources(ScriptCompiler PRIVATE ${rhi_adapater_interface_files})
    get_filename_component(module_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    source_group(TREE "${module_root}" FILES ${rhi_adapater_interface_files})
endif()

get_filename_component(gamesprout_bin_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Bin" ABSOLUTE
)
if(TARGET GameSproutEditor)
    set(module_output_directory "$<TARGET_FILE_DIR:GameSproutEditor>/Logic/Module")
elseif(APPLE)
    set(module_output_directory
        "${gamesprout_bin_directory}/GameSproutEditor.app/Contents/MacOS/Logic/Module")
else()
    set(module_output_directory "${gamesprout_bin_directory}/Logic/Module")
endif()

set_target_properties(ScriptCompiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    LIBRARY_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    COMPILE_PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
)

get_filename_component(script_sdk_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutRuntime/Api" ABSOLUTE)
function(ScriptCompilerEscapeCString input output)
    string(REPLACE "\\" "\\\\" escaped "${input}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    string(REPLACE "\n" "\\n" escaped "${escaped}")
    string(REPLACE "\r" "\\r" escaped "${escaped}")
    set(${output} "${escaped}" PARENT_SCOPE)
endfunction()

if(WIN32 AND MSVC AND CMAKE_CONFIGURATION_TYPES)
    set(script_per_configuration TRUE)
    set(script_build_configuration "$<CONFIG>")
    set(script_configurations ${CMAKE_CONFIGURATION_TYPES})
else()
    set(script_build_configuration "${CMAKE_BUILD_TYPE}")
    if(NOT script_build_configuration)
        set(script_build_configuration "Debug")
    endif()
    set(script_configurations "${script_build_configuration}")
endif()

set(script_base_flags "${CMAKE_CXX_FLAGS}")
set(script_crt "${CMAKE_MSVC_RUNTIME_LIBRARY}")
if(WIN32 AND MSVC)
    include("${CMAKE_CURRENT_LIST_DIR}/../../Tools/MsvcDebugOptimization.cmake")
    string(APPEND script_base_flags " /utf-8 /Zc:__cplusplus /Zc:preprocessor")
    if(NOT GAMESPROUT_SCRIPT_REFLECTION_COMPILER)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            set(GAMESPROUT_SCRIPT_REFLECTION_COMPILER "${CMAKE_CXX_COMPILER}")
        else()
            find_program(GAMESPROUT_SCRIPT_REFLECTION_COMPILER NAMES clang-cl
                HINTS "$ENV{LLVM_ROOT}/bin" "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin"
                    "$ENV{VCINSTALLDIR}/Tools/Llvm/bin" "$ENV{ProgramFiles}/LLVM/bin")
        endif()
    endif()
    if(GAMESPROUT_SCRIPT_REFLECTION_COMPILER)
        set(script_reflection_program "${GAMESPROUT_SCRIPT_REFLECTION_COMPILER}")
        execute_process(COMMAND "${script_reflection_program}" --version
            OUTPUT_VARIABLE script_reflection_version OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET TIMEOUT 10)
    endif()
    string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" script_windows_architecture)
    if(script_windows_architecture STREQUAL "x64")
        set(script_reflection_target x86_64-pc-windows-msvc)
    elseif(script_windows_architecture STREQUAL "x86")
        set(script_reflection_target i686-pc-windows-msvc)
    elseif(script_windows_architecture STREQUAL "arm64")
        set(script_reflection_target aarch64-pc-windows-msvc)
    endif()
    if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        set(script_crt "$<TARGET_GENEX_EVAL:ScriptCompiler,$<TARGET_PROPERTY:ScriptCompiler,MSVC_RUNTIME_LIBRARY>>")
    else()
        set(script_crt "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()

    # A GUI-started Editor does not inherit a developer prompt. Keep the same
    # SDK and library search paths that CMake used to build the host kit.
    file(TO_CMAKE_PATH "$ENV{VCToolsInstallDir}" script_vc_tools)
    if(NOT script_vc_tools AND CMAKE_LINKER MATCHES "/bin/Host[^/]+/[^/]+/link\\.exe$")
        string(REGEX REPLACE "/bin/Host[^/]+/[^/]+/link\\.exe$" "" script_vc_tools "${CMAKE_LINKER}")
    endif()
    file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" script_windows_sdk)
    set(script_windows_sdk_version "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    if(CMAKE_RC_COMPILER MATCHES "^(.+)/bin/([0-9.]+)/[^/]+/rc\\.exe$")
        if(NOT script_windows_sdk)
            set(script_windows_sdk "${CMAKE_MATCH_1}")
        endif()
        if(NOT script_windows_sdk_version)
            set(script_windows_sdk_version "${CMAKE_MATCH_2}")
        endif()
    endif()
    foreach(tool IN ITEMS CMAKE_CXX_COMPILER CMAKE_LINKER CMAKE_RC_COMPILER CMAKE_MT)
        if(IS_ABSOLUTE "${${tool}}")
            get_filename_component(tool_directory "${${tool}}" DIRECTORY)
            list(APPEND script_windows_tool_directories "${tool_directory}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES script_windows_tool_directories)
    ScriptCompilerEscapeCString("$ENV{INCLUDE}" script_windows_include_escaped)
    ScriptCompilerEscapeCString("$ENV{LIB}" script_windows_lib_escaped)
    ScriptCompilerEscapeCString("$ENV{LIBPATH}" script_windows_libpath_escaped)
    target_compile_options(ScriptCompiler PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
else()
    set(script_reflection_program "${CMAKE_CXX_COMPILER}")
    set(script_reflection_version "${CMAKE_CXX_COMPILER_VERSION}")
endif()
ScriptCompilerEscapeCString("${script_reflection_program}" script_reflection_program_escaped)
ScriptCompilerEscapeCString("${script_reflection_version}" script_reflection_version_escaped)
ScriptCompilerEscapeCString("${script_base_flags}" script_base_flags_escaped)

# Reflection and compilation use the same per-configuration flags, including
# ABI-affecting defines. Tokenize them once with CMake's native command parser.
foreach(configuration IN LISTS script_configurations)
    string(TOUPPER "${configuration}" configuration_upper)
    set(script_native_configuration_flags "${CMAKE_CXX_FLAGS_${configuration_upper}}")
    # Keep Debug symbols and assertions for GameLogic with an optimized frame loop.
    if(configuration_upper STREQUAL "DEBUG")
        if(WIN32 AND MSVC)
            GameSproutOptimizeMsvcDebugFlags("${script_native_configuration_flags}" script_native_configuration_flags)
        elseif(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
            string(PREPEND script_native_configuration_flags "-O2 ")
        endif()
    endif()
    ScriptCompilerEscapeCString("${script_native_configuration_flags}" configuration_flags_escaped)
    separate_arguments(script_ast_flags NATIVE_COMMAND "${script_base_flags} ${script_native_configuration_flags}")
    set(ast_flag_literals "")
    foreach(script_flag IN LISTS script_ast_flags)
        ScriptCompilerEscapeCString("${script_flag}" script_flag_escaped)
        list(APPEND ast_flag_literals "\"${script_flag_escaped}\"")
    endforeach()
    list(JOIN ast_flag_literals "," ast_flags_cpp)
    if(script_per_configuration)
        string(APPEND script_configuration_flags_escaped "$<$<CONFIG:${configuration}>:${configuration_flags_escaped}>")
        string(APPEND script_ast_flags_cpp "$<$<CONFIG:${configuration}>:${ast_flags_cpp}>")
    else()
        set(script_configuration_flags_escaped "${configuration_flags_escaped}")
        set(script_ast_flags_cpp "${ast_flags_cpp}")
    endif()
endforeach()
if(WIN32 AND MSVC)
    configure_file(ScriptToolchain.h.in "${CMAKE_CURRENT_BINARY_DIR}/ScriptToolchain.configured.h" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/ScriptToolchain.h"
        INPUT "${CMAKE_CURRENT_BINARY_DIR}/ScriptToolchain.configured.h" TARGET ScriptCompiler)
    target_include_directories(ScriptCompiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
else()
    configure_file(ScriptToolchain.h.in "${CMAKE_CURRENT_BINARY_DIR}/ScriptToolchain.h" @ONLY)
    target_include_directories(ScriptCompiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# The external IDE invokes the same compiler module as Editor. This executable
# belongs to ScriptCompiler and does not bootstrap an Editor, Runtime or View.
add_executable(GameSproutScriptBuild Tools/ScriptCompilerCommand.cpp)
target_compile_features(GameSproutScriptBuild PRIVATE cxx_std_20)
target_include_directories(GameSproutScriptBuild PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutEditor/Api")
target_link_libraries(GameSproutScriptBuild PRIVATE ScriptCompiler
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt${QT_VERSION_MAJOR}::Core)
if(TARGET GameSproutEditor)
    set(script_command_directory "$<TARGET_FILE_DIR:GameSproutEditor>/Tools")
    add_dependencies(GameSproutEditor GameSproutScriptBuild)
else()
    # Derive from the final module output property so isolated test projects can
    # redirect ScriptCompiler without this tool writing into the user's Bin.
    set(script_command_directory "$<TARGET_FILE_DIR:ScriptCompiler>/../../Tools")
endif()
set_target_properties(GameSproutScriptBuild PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${script_command_directory}/$<0:>")
if(APPLE)
    set_property(TARGET GameSproutScriptBuild APPEND PROPERTY BUILD_RPATH "@loader_path/../Logic/Module")
elseif(WIN32)
    add_custom_command(TARGET GameSproutScriptBuild POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            $<TARGET_RUNTIME_DLLS:GameSproutScriptBuild> "$<TARGET_FILE_DIR:GameSproutScriptBuild>"
        COMMAND_EXPAND_LISTS
        VERBATIM)
elseif(UNIX)
    set_property(TARGET GameSproutScriptBuild APPEND PROPERTY BUILD_RPATH "$ORIGIN/../Logic/Module")
endif()
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES "${CMAKE_CURRENT_SOURCE_DIR}/Tools/ScriptCompilerCommand.cpp")
