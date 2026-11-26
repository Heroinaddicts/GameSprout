cmake_minimum_required(VERSION 3.21)

if(NOT IS_DIRECTORY "${FOUNDATION_RUNTIME_DIRECTORY}")
    message(FATAL_ERROR "Foundation runtime directory does not exist: ${FOUNDATION_RUNTIME_DIRECTORY}")
endif()

foreach(runtimeFile IN LISTS FOUNDATION_RUNTIME_FILES)
    get_filename_component(runtimeName "${runtimeFile}" NAME)
    file(COPY_FILE "${runtimeFile}" "${FOUNDATION_RUNTIME_DIRECTORY}/${runtimeName}"
        ONLY_IF_DIFFERENT RESULT copyResult)
    if(NOT copyResult STREQUAL "0")
        message(FATAL_ERROR "Cannot deploy ${runtimeFile} to ${FOUNDATION_RUNTIME_DIRECTORY}: ${copyResult}")
    endif()
endforeach()
