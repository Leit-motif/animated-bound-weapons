cmake_minimum_required(VERSION 3.21)

project(AnimatedBoundWeapons VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Pin: b93280e Fix light-mod count (#97). Bump this only as its own change.
set(ABW_COMMONLIBSSE_GIT_TAG "b93280e832f263dbef44e44cbe2936622a02f91a")

include(FetchContent)
FetchContent_Declare(
    CommonLibSSE
    GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
    GIT_TAG ${ABW_COMMONLIBSSE_GIT_TAG}
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(CommonLibSSE)

get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)
include("${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake")

set(SOURCES
    src/Main.cpp
    src/Activate.cpp
    src/BoundFacts.cpp
    src/EngineCalls.cpp
    src/FloaterSetup.cpp
    src/Forms.cpp
    src/FormListStorage.cpp
    src/SpellFilters.cpp
    src/PlayerSpells.cpp
    src/PowerGrant.cpp
    src/Menu.cpp
    src/Loadout.cpp
)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR "Animated Bound Weapons"
    SOURCES ${SOURCES}
)

target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE include extern)
target_compile_definitions(${PROJECT_NAME} PRIVATE MOD_NAME="Animated Bound Weapons")

set(MOD_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../AnimatedBoundWeapons/SKSE/Plugins")
add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MOD_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${MOD_OUTPUT_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    VERBATIM
)
