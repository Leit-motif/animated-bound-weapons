cmake_minimum_required(VERSION 3.21)

project(AnimatedBoundWeapons VERSION 1.5.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Pin: alandtse fork tag v9.1.0, by commit so the tag cannot move under a release. The first NG line that loads on Skyrim 1.7.99+: it reads
# the format-5 Address Library, versions the 1.7.99 SkyrimVM/PlayerCharacter layouts, and sets
# the SKSE AddressLibraryV5 flag. Bump this only as its own change.
set(ABW_COMMONLIBSSE_GIT_TAG "a898f469851c464d05137bb74b069dd234897643")

include(FetchContent)
FetchContent_Declare(
    CommonLibSSE
    GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
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
    src/Strings.cpp
    src/PluginVmad.cpp
    src/BoundAmmo.cpp
    src/TargetFilter.cpp
    src/InstructionLength.cpp
)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR "Animated Bound Weapons"
    SOURCES ${SOURCES}
)

target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)

# TargetFilter decodes instruction boundaries with the hde64 that CommonLibSSE-NG vendors
# (SKSE_SUPPORT_PATCH_SAFETY) and links; only the header path is needed here.
FetchContent_GetProperties(hde64 SOURCE_DIR ABW_HDE64_SOURCE_DIR)
if(NOT ABW_HDE64_SOURCE_DIR)
    message(FATAL_ERROR "hde64 not populated: CommonLibSSE-NG SKSE_SUPPORT_PATCH_SAFETY must stay ON")
endif()
set_source_files_properties(src/InstructionLength.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    INCLUDE_DIRECTORIES "${ABW_HDE64_SOURCE_DIR}/src/hde")
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
