cmake_minimum_required(VERSION 3.21)
project(LightingBakeKernelChecks LANGUAGES CXX)
enable_testing()

set(gamesprout "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(LightingBakeKernelChecks LightingBakeKernelChecks.cpp
    "${gamesprout}/Tools/Environment/LightingBakeKernels.cpp")
target_compile_features(LightingBakeKernelChecks PRIVATE cxx_std_20)
target_include_directories(LightingBakeKernelChecks PRIVATE
    "${gamesprout}/Tools/Environment"
    "${gamesprout}/GameSproutRuntime/Api"
    "${gamesprout}/PreparedResources")
add_test(NAME Lighting.NativeBakeKernels COMMAND LightingBakeKernelChecks)
