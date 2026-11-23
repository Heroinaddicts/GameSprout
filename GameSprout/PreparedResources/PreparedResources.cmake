include_guard(GLOBAL)
if(NOT TARGET PreparedResources::PreparedResources)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/PreparedResources")
endif()
