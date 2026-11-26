cmake_minimum_required(VERSION 3.21)
project(GameSprout VERSION 0.1 LANGUAGES CXX)

# Discover SDL3 in the common parent so backend deploy rules and Player
# link rules share the same package target across sibling directories.
include("${CMAKE_CURRENT_LIST_DIR}/Tools/FindGameSproutSDL3.cmake")
GameSproutFindSDL3(SDL3::SDL3-shared)

# Define the in-tree SDK targets before adding consumers of their public APIs.
add_subdirectory(Foundation)
add_subdirectory(GRHI)
add_subdirectory(GameSproutRuntime)

# Runtime adds GPhysics. Editor adds Module, View and the desktop Player template.
add_subdirectory(GameSproutAgent)
add_subdirectory(GameSproutEditor)
add_subdirectory(GameSproutHub)
if(NOT TARGET GameSproutPlayer)
    add_subdirectory(GameSproutPlayer)
endif()
