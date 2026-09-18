cmake_minimum_required(VERSION 3.21)

# CMAKE_MSVC_RUNTIME_LIBRARY only takes effect with this policy, and it has to be
# set before project(). The dynamic CRT is what CommonLibSSE-NG's own presets use
# and what the x64-windows-static-md triplet produces; plugin and library must
# agree or the build silently links a mismatched runtime.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(RFAB_SurvivalLayer_Native VERSION 0.5.0 LANGUAGES CXX)

# Dependencies come from two places, deliberately:
#
#   - fmt / spdlog / simpleini / rapidcsv  -> vcpkg (see vcpkg.json)
#   - CommonLibSSE-NG                      -> add_subdirectory of a local checkout
#
# There is no commonlibsse-ng port in vcpkg's default registry. Building it from
# source next to the plugin is the normal arrangement for SKSE plugins, not a
# workaround - TrueHUD does exactly this.
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# CommonLibSSE-NG reads rapidcsv through a plain include path, not a package.
find_path(RAPIDCSV_INCLUDE_DIRS "rapidcsv.h" REQUIRED)

if(NOT DEFINED ENV{CommonLibSSEPath})
    message(FATAL_ERROR
        "CommonLibSSEPath is not set. Point it at the CommonLibSSE-NG checkout, e.g.\n"
        "  setx CommonLibSSEPath R:/Games/develop/CommonLibSSE-NG-3.7.0\n"
        "Forward slashes on purpose. A backslash starts an escape in a CMake string,\n"
        "and this very message used to abort the configure with 'Invalid character\n"
        "escape' rather than print - so the one thing it existed to say never got said.")
endif()

# The game here is SE 1.5.97. AE stays on so one binary keeps working if the
# runtime is ever updated; VR is off because we would need a third address
# database for it and there is no VR install to test against.
set(ENABLE_SKYRIM_SE ON  CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON  CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS      OFF CACHE BOOL "" FORCE)
add_subdirectory("$ENV{CommonLibSSEPath}" "${CMAKE_CURRENT_BINARY_DIR}/commonlibsse-ng" EXCLUDE_FROM_ALL)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/Settings.cpp
    src/Menu/RSLMenu.cpp
    src/Menu/BarWidget.cpp
    src/Core/Needs.cpp
    src/Core/Notify.cpp
    src/Core/Climate.cpp
    src/Core/ClimateMap.cpp
    src/Core/RfabPatch.cpp
    src/Core/Trees.cpp
    src/Core/Events.cpp
    src/Core/ColdVisual.cpp
    src/Core/Cough.cpp
    src/Core/ColdScreen.cpp
    src/Core/Teleport.cpp
    src/Core/Disease.cpp
    src/Core/Illness.cpp
    src/Core/Illnesses.cpp
    src/Core/HitIllness.cpp
    src/Core/ColdIllness.cpp
    src/Core/LesionIllness.cpp
    src/Core/RfabIllness.cpp
    src/Core/Player.cpp
    src/Core/Random.cpp
    src/Core/Elemental.cpp
    src/Core/BodyCondition.cpp
    src/Core/Hypothermia.cpp
    src/Core/Forms.cpp
    src/Core/Penalties.cpp
    src/Core/WarmAnim.cpp
    src/Core/Shelter.cpp
    src/Core/Placement.cpp
    src/Core/Campfire.cpp
    src/Core/Bedroll.cpp
    src/Core/TakeDown.cpp
    src/Core/Teardown.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /Zc:preprocessor /EHsc /MP /utf-8 /W4 /Zi)
    # Symbols even in Release. A crash inside this DLL is otherwise just an
    # offset, and NetScriptFramework cannot resolve it without a pdb.
    target_link_options(${PROJECT_NAME} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endif()

# CommonLibSSE-NG itself only needs C++20; nothing here asks for more.
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)

# CommonLibSSE-NG adds this PRIVATE to its own target, so it does not propagate
# here even though REL/Relocation.h (pulled in through RE/Skyrim.h) needs it.
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${RAPIDCSV_INCLUDE_DIRS}
)

# Ships next to RFAB.dll and the rest. Everything else in the mod (scripts, MCM,
# esp, swf) is built by its own step; only the plugin comes from CMake.
#
# The game must be fully closed before rebuilding - it holds the DLL open while
# running and this copy then fails with a permission error.
set(MOD_SKSE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SKSE/Plugins")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MOD_SKSE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "${MOD_SKSE_DIR}/_RSL_Core.dll"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PDB_FILE:${PROJECT_NAME}> "${MOD_SKSE_DIR}/_RSL_Core.pdb"
)
