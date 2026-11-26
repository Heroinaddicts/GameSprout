cmake_minimum_required(VERSION 3.21)

if(NOT EXISTS "${FOUNDATION_RUNTIME_BINARY}" OR IS_DIRECTORY "${FOUNDATION_RUNTIME_BINARY}")
    message(FATAL_ERROR "Foundation runtime binary is missing: ${FOUNDATION_RUNTIME_BINARY}")
endif()
if(NOT IS_DIRECTORY "${FOUNDATION_RUNTIME_DIRECTORY}")
    message(FATAL_ERROR "Foundation runtime directory does not exist: ${FOUNDATION_RUNTIME_DIRECTORY}")
endif()

foreach(runtimeName IN LISTS FOUNDATION_REQUIRED_APP_LOCAL_RUNTIME_FILES)
    set(runtimeFile "${FOUNDATION_RUNTIME_DIRECTORY}/${runtimeName}")
    if(NOT EXISTS "${runtimeFile}" OR IS_DIRECTORY "${runtimeFile}")
        message(FATAL_ERROR "Required application-local runtime file is missing: ${runtimeFile}")
    endif()
endforeach()

# Windows supplies API sets and the dependencies of its system DLLs.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${FOUNDATION_RUNTIME_BINARY}"
    DIRECTORIES "${FOUNDATION_RUNTIME_DIRECTORY}"
    PRE_EXCLUDE_REGEXES "^(api|ext)-ms-.*\\.dll$"
    POST_EXCLUDE_REGEXES "/[Ss][Yy][Ss][Tt][Ee][Mm]32/" "/[Ss][Yy][Ss][Ww][Oo][Ww]64/"
    UNRESOLVED_DEPENDENCIES_VAR unresolvedDependencies)

if(unresolvedDependencies)
    list(JOIN unresolvedDependencies ", " unresolvedNames)
    message(FATAL_ERROR "Unresolved runtime dependencies of ${FOUNDATION_RUNTIME_BINARY}: ${unresolvedNames}")
endif()
