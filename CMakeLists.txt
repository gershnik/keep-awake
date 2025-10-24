cmake_minimum_required(VERSION 3.25)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithDebInfo" CACHE STRING "" FORCE)

#Set sane base flags to avoid reliance on whatever defaults are set from outside
set(CMAKE_C_FLAGS "/Zi /DWIN32 /D_WINDOWS")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG  "")
set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "")
set(CMAKE_RC_FLAGS "/nologo /DWIN32")
set(CMAKE_RC_FLAGS_DEBUG "/D_DEBUG")
set(CMAKE_RC_FLAGS_RELWITHDEBINFO "/DNDEBUG")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "")
set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "")
set(CMAKE_C_STANDARD_LIBRARIES "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:DEBUG>:Debug>")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_PCH_INSTANTIATE_TEMPLATES ON)

add_compile_options(
    /Zi

    /sdl
    /GR 
    /EHsc
    /bigobj
    "$<$<CONFIG:DEBUG>:/Od;/Ob0>"
    "$<$<CONFIG:RELWITHDEBINFO>:/O2;/Ob3;/Oi;/Os;/GT;/Gy;/Qpar>" #;/GL>"
)

add_compile_definitions(
    "$<$<CONFIG:DEBUG>:_DEBUG>"
    "$<$<CONFIG:RELWITHDEBINFO>:NDEBUG>"
)

add_link_options(
    "$<$<CONFIG:DEBUG>:/DEBUG:FASTLINK;/INCREMENTAL>"
    "$<$<CONFIG:RELWITHDEBINFO>:/DEBUG;/RELEASE;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF>" #;/LTCG:incremental>"
)

project(keep-awake)

include(FetchContent)

set(DECLARED_DEPENDENCIES "")

FetchContent_Declare(argum
    GIT_REPOSITORY  https://github.com/gershnik/argum.git
    GIT_TAG         v2.6
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    TRUE
)
list(APPEND DECLARED_DEPENDENCIES argum)

FetchContent_Declare(ctre
    GIT_REPOSITORY  https://github.com/hanickadot/compile-time-regular-expressions.git
    GIT_TAG         v3.10.0
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    TRUE
    SOURCE_SUBDIR   single-header
)
list(APPEND DECLARED_DEPENDENCIES ctre)

FetchContent_MakeAvailable(${DECLARED_DEPENDENCIES})

get_directory_property(KNOWN_SUBDIRECTORIES SUBDIRECTORIES)
foreach(dir ${KNOWN_SUBDIRECTORIES})
    if (IS_DIRECTORY ${dir})
        foreach(dep ${DECLARED_DEPENDENCIES})
            #check if the subdirectory is "under" the dependency source dir
            string(FIND ${dir} ${${dep}_SOURCE_DIR} match_pos)
            if (match_pos EQUAL 0)
                #and, if so, exclude it from all to prevent installation
                set_property(DIRECTORY ${dir} PROPERTY EXCLUDE_FROM_ALL YES)
                break()
            endif()
        endforeach()
    endif()
endforeach()

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/version.json VERSION_JSON)
string(JSON BUILD_MAJOR_VERSION GET ${VERSION_JSON} "BUILD_MAJOR_VERSION")
string(JSON BUILD_MINOR_VERSION GET ${VERSION_JSON} "BUILD_MINOR_VERSION")
string(JSON BUILD_PATCH_VERSION GET ${VERSION_JSON} "BUILD_PATCH_VERSION")


add_executable(keep-awake)

target_link_libraries(keep-awake PRIVATE
    argum
    Wtsapi32.lib
)

target_include_directories(keep-awake PRIVATE
    ${ctre_SOURCE_DIR}/single-header
)

target_compile_options(keep-awake PRIVATE
    /W4;/WX
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor;/MP>
    /utf-8
)

target_compile_definitions(keep-awake PRIVATE
    _WIN32_WINNT=0x0A00
    UNICODE
    _UNICODE
    NOMINMAX
)

target_sources(keep-awake PRIVATE
    keep-awake.cpp
    pch.h
    keep-awake.rc
    keep-awake.ico
    version.rc2
    version.rc2.in
    resource.h
)

set_source_files_properties(keep-awake.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/keep-awake.ico)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.rc2.in ${CMAKE_CURRENT_SOURCE_DIR}/version.rc2 NEWLINE_STYLE WIN32 @ONLY)

target_precompile_headers(keep-awake PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)




