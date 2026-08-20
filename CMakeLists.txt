cmake_minimum_required(VERSION 3.14)
project(socl)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SOCL_IS_TOP_LEVEL ON)
else()
    set(SOCL_IS_TOP_LEVEL OFF)
endif()

option(SOCL_BUILD_TESTS "Build SOCL tests" ${SOCL_IS_TOP_LEVEL})
option(SOCL_BUILD_EXAMPLES "Build SOCL examples" ${SOCL_IS_TOP_LEVEL})
option(SOCL_BUILD_DOCS "Build SOCL API documentation with Doxygen" ON)
option(SOCL_DOCS_ONLY "Configure only the SOCL API documentation target" OFF)

if(SOCL_DOCS_ONLY)
    set(SOCL_BUILD_DOCS ON)
endif()

if(SOCL_BUILD_DOCS)
    find_package(Doxygen REQUIRED)

    set(DOXYGEN_PROJECT_NAME "SOCL")
    set(DOXYGEN_PROJECT_BRIEF "Simple Open Compute Library based on Vulkan")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    set(DOXYGEN_EXTRACT_ALL NO)
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_EXCLUDE_SYMBOLS "socl::detail::*")
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    set(DOXYGEN_WARN_IF_DOC_ERROR YES)
    set(DOXYGEN_WARN_NO_PARAMDOC YES)
    set(DOXYGEN_WARN_AS_ERROR YES)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)

    doxygen_add_docs(socl_docs
        "${PROJECT_SOURCE_DIR}/include/socl"
        "${PROJECT_SOURCE_DIR}/docs"
        COMMENT "Generate SOCL public API documentation"
    )
endif()

if(SOCL_DOCS_ONLY)
    return()
endif()

if(SOCL_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(FetchContent)
find_package(Vulkan REQUIRED)

if(WIN32 AND DEFINED ENV{VULKAN_SDK})
    find_package(shaderc QUIET CONFIG
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES
            Lib/cmake/shaderc
            lib/cmake/shaderc
            share/cmake/shaderc
    )
else()
    find_package(shaderc QUIET CONFIG)
endif()

if(TARGET shaderc::shaderc)
    set(SOCL_SHADERC_TARGET shaderc::shaderc)
elseif(TARGET shaderc::shaderc_shared)
    set(SOCL_SHADERC_TARGET shaderc::shaderc_shared)
elseif(TARGET shaderc_shared)
    set(SOCL_SHADERC_TARGET shaderc_shared)
elseif(TARGET shaderc)
    set(SOCL_SHADERC_TARGET shaderc)
elseif(NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SOCL_SHADERC_PC QUIET IMPORTED_TARGET shaderc)
    endif()

    if(TARGET PkgConfig::SOCL_SHADERC_PC)
        set(SOCL_SHADERC_TARGET PkgConfig::SOCL_SHADERC_PC)
    else()
        message(FATAL_ERROR
            "shaderc was not found. Install a shaderc package that provides a supported "
            "CMake target (shaderc::shaderc, shaderc::shaderc_shared, shaderc_shared, or "
            "shaderc), or install a shaderc pkg-config module."
        )
    endif()
elseif(WIN32 AND DEFINED ENV{VULKAN_SDK})
    find_path(SOCL_SHADERC_INCLUDE_DIR
        NAMES shaderc/shaderc.hpp
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES Include include
        NO_DEFAULT_PATH
    )
    find_library(SOCL_SHADERC_LIBRARY_RELEASE
        NAMES shaderc_combined shaderc
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES Lib lib
        NO_DEFAULT_PATH
    )
    find_library(SOCL_SHADERC_LIBRARY_DEBUG
        NAMES shaderc_combinedd shadercd
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES Lib lib
        NO_DEFAULT_PATH
    )

    if(NOT SOCL_SHADERC_INCLUDE_DIR OR NOT SOCL_SHADERC_LIBRARY_RELEASE)
        message(FATAL_ERROR
            "shaderc was not found in VULKAN_SDK ($ENV{VULKAN_SDK}). Expected "
            "shaderc/shaderc.hpp under Include and shaderc_combined.lib or shaderc.lib "
            "under Lib."
        )
    endif()

    add_library(socl_shaderc_sdk UNKNOWN IMPORTED)
    set_target_properties(socl_shaderc_sdk PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SOCL_SHADERC_INCLUDE_DIR}"
        IMPORTED_LOCATION "${SOCL_SHADERC_LIBRARY_RELEASE}"
    )

    if(SOCL_SHADERC_LIBRARY_DEBUG)
        set_target_properties(socl_shaderc_sdk PROPERTIES
            IMPORTED_LOCATION_DEBUG "${SOCL_SHADERC_LIBRARY_DEBUG}"
        )
    else()
        set_target_properties(socl_shaderc_sdk PROPERTIES
            IMPORTED_LOCATION_DEBUG "${SOCL_SHADERC_LIBRARY_RELEASE}"
        )
    endif()

    set(SOCL_SHADERC_TARGET socl_shaderc_sdk)
else()
    message(FATAL_ERROR
        "shaderc was not found. Install a shaderc package that provides a supported "
        "CMake target (shaderc::shaderc, shaderc::shaderc_shared, shaderc_shared, or "
        "shaderc), or install a shaderc pkg-config module. On Windows, ensure VULKAN_SDK "
        "points to the Vulkan SDK installation."
    )
endif()

FetchContent_Declare(
  vk-bootstrap
  GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
  GIT_TAG        v1.3.302
)
FetchContent_Declare(
  VulkanMemoryAllocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG        v3.2.1
)
FetchContent_MakeAvailable(vk-bootstrap VulkanMemoryAllocator)
set_property(TARGET vk-bootstrap PROPERTY POSITION_INDEPENDENT_CODE ON)

add_subdirectory(src)

add_library(socl SHARED ${SOURCES})
set_target_properties(socl PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_compile_features(socl PUBLIC cxx_std_20)
target_include_directories(socl
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)
target_link_libraries(socl PUBLIC Vulkan::Vulkan vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator)
target_link_libraries(socl PRIVATE ${SOCL_SHADERC_TARGET})

if(SOCL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(SOCL_BUILD_TESTS)
    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()
    enable_testing()

    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)

    add_executable(socl_tests ${TEST_SOURCES})
    target_link_libraries(socl_tests PRIVATE socl GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(socl_tests)
endif()
