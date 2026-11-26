# Shared by standalone projects and their subdirectories.
include_guard(DIRECTORY)

# JOM can misread localized MSVC /showIncludes prefixes in generated rules.
if(CMAKE_HOST_WIN32 AND CMAKE_GENERATOR STREQUAL "NMake Makefiles JOM")
    set(CMAKE_DEPENDS_USE_COMPILER OFF CACHE BOOL "Use compiler-generated dependencies")
    if(NOT CMAKE_DEPENDS_USE_COMPILER)
        # Keep generated rules and try_compile projects in the same mode, even
        # when this helper is first included after a language was enabled.
        set(CMAKE_C_DEPENDS_USE_COMPILER OFF)
        set(CMAKE_CXX_DEPENDS_USE_COMPILER OFF)
        list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_DEPENDS_USE_COMPILER)
    endif()
endif()

cmake_host_system_information(RESULT FOUNDATION_BUILD_CORES QUERY NUMBER_OF_LOGICAL_CORES)
if(NOT FOUNDATION_BUILD_CORES MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "Cannot determine the host CPU core count.")
endif()

# Ninja starts jobs automatically; cap compilation at the host core count.
get_property(foundation_job_pools GLOBAL PROPERTY JOB_POOLS)
if(NOT "foundation_compile=${FOUNDATION_BUILD_CORES}" IN_LIST foundation_job_pools)
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS foundation_compile=${FOUNDATION_BUILD_CORES})
endif()
set(CMAKE_JOB_POOL_COMPILE foundation_compile)
set(CMAKE_AUTOGEN_PARALLEL "${FOUNDATION_BUILD_CORES}")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/MP${FOUNDATION_BUILD_CORES}>")

# CMAKE_BUILD_PARALLEL_LEVEL is a build-time environment variable, not a
# configure-time CMake variable. Generate a portable entry for every build tree.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/BuildParallel.cmake.in"
        "${CMAKE_BINARY_DIR}/build-parallel.cmake" @ONLY)
    message(STATUS "Parallel build: ${FOUNDATION_BUILD_CORES} logical cores; cmake -P \"${CMAKE_BINARY_DIR}/build-parallel.cmake\"")
endif()
