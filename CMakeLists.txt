cmake_minimum_required(VERSION 3.23)
project(vucol VERSION 0.0.10 LANGUAGES CXX)

include(GNUInstallDirs)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(VUCOL_IS_TOP_LEVEL ON)
else()
    set(VUCOL_IS_TOP_LEVEL OFF)
endif()

option(VUCOL_BUILD_TESTS "Build VUCOL tests" ${VUCOL_IS_TOP_LEVEL})
option(VUCOL_BUILD_EXAMPLES "Build VUCOL examples" ${VUCOL_IS_TOP_LEVEL})
option(VUCOL_BUILD_DOCS "Build VUCOL API documentation with Doxygen" ${VUCOL_IS_TOP_LEVEL})
option(VUCOL_DOCS_ONLY "Configure only the VUCOL API documentation target" OFF)

if(VUCOL_DOCS_ONLY)
    set(VUCOL_BUILD_DOCS ON)
endif()

if(VUCOL_BUILD_DOCS)
    find_package(Doxygen REQUIRED)

    set(DOXYGEN_PROJECT_NAME "VUCOL")
    set(DOXYGEN_PROJECT_BRIEF "Simple Open Compute Library based on Vulkan")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    set(DOXYGEN_EXTRACT_ALL NO)
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_EXCLUDE_SYMBOLS "vucol::detail::*")
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    set(DOXYGEN_WARN_IF_DOC_ERROR YES)
    set(DOXYGEN_WARN_NO_PARAMDOC YES)
    set(DOXYGEN_WARN_AS_ERROR YES)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)

    doxygen_add_docs(vucol_docs
        "${PROJECT_SOURCE_DIR}/include/vucol"
        "${PROJECT_SOURCE_DIR}/docs"
        COMMENT "Generate VUCOL public API documentation"
    )
endif()

if(VUCOL_DOCS_ONLY)
    return()
endif()

if(VUCOL_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(FetchContent)
find_package(Vulkan 1.3 REQUIRED)

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
    set(VUCOL_SHADERC_TARGET shaderc::shaderc)
elseif(TARGET shaderc::shaderc_shared)
    set(VUCOL_SHADERC_TARGET shaderc::shaderc_shared)
elseif(TARGET shaderc_shared)
    set(VUCOL_SHADERC_TARGET shaderc_shared)
elseif(TARGET shaderc)
    set(VUCOL_SHADERC_TARGET shaderc)
elseif(NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(VUCOL_SHADERC_PC QUIET IMPORTED_TARGET shaderc)
    endif()

    if(TARGET PkgConfig::VUCOL_SHADERC_PC)
        set(VUCOL_SHADERC_TARGET PkgConfig::VUCOL_SHADERC_PC)
    else()
        message(FATAL_ERROR
            "shaderc was not found. Install a shaderc package that provides a supported "
            "CMake target (shaderc::shaderc, shaderc::shaderc_shared, shaderc_shared, or "
            "shaderc), or install a shaderc pkg-config module."
        )
    endif()
elseif(WIN32 AND DEFINED ENV{VULKAN_SDK})
    find_path(VUCOL_SHADERC_INCLUDE_DIR
        NAMES shaderc/shaderc.hpp
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES Include include
        NO_DEFAULT_PATH
    )
    find_library(VUCOL_SHADERC_LIBRARY_RELEASE
        NAMES shaderc_combined shaderc
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES Lib lib
        NO_DEFAULT_PATH
    )
    find_library(VUCOL_SHADERC_LIBRARY_DEBUG
        NAMES shaderc_combinedd shadercd
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES Lib lib
        NO_DEFAULT_PATH
    )

    if(NOT VUCOL_SHADERC_INCLUDE_DIR OR NOT VUCOL_SHADERC_LIBRARY_RELEASE)
        message(FATAL_ERROR
            "shaderc was not found in VULKAN_SDK ($ENV{VULKAN_SDK}). Expected "
            "shaderc/shaderc.hpp under Include and shaderc_combined.lib or shaderc.lib "
            "under Lib."
        )
    endif()

    add_library(vucol_shaderc_sdk UNKNOWN IMPORTED)
    set_target_properties(vucol_shaderc_sdk PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${VUCOL_SHADERC_INCLUDE_DIR}"
        IMPORTED_LOCATION "${VUCOL_SHADERC_LIBRARY_RELEASE}"
    )

    if(VUCOL_SHADERC_LIBRARY_DEBUG)
        set_target_properties(vucol_shaderc_sdk PROPERTIES
            IMPORTED_LOCATION_DEBUG "${VUCOL_SHADERC_LIBRARY_DEBUG}"
        )
    else()
        set_target_properties(vucol_shaderc_sdk PROPERTIES
            IMPORTED_LOCATION_DEBUG "${VUCOL_SHADERC_LIBRARY_RELEASE}"
        )
    endif()

    set(VUCOL_SHADERC_TARGET vucol_shaderc_sdk)
else()
    message(FATAL_ERROR
        "shaderc was not found. Install a shaderc package that provides a supported "
        "CMake target (shaderc::shaderc, shaderc::shaderc_shared, shaderc_shared, or "
        "shaderc), or install a shaderc pkg-config module. On Windows, ensure VULKAN_SDK "
        "points to the Vulkan SDK installation."
    )
endif()

find_package(vk-bootstrap QUIET CONFIG)
if(NOT TARGET vk-bootstrap::vk-bootstrap)
    FetchContent_Declare(
      vk-bootstrap
      GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
      GIT_TAG        v1.3.302
    )
    FetchContent_MakeAvailable(vk-bootstrap)
    set_property(TARGET vk-bootstrap PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

find_package(VulkanMemoryAllocator 3.2 QUIET CONFIG)
if(NOT TARGET GPUOpen::VulkanMemoryAllocator)
    FetchContent_Declare(
      VulkanMemoryAllocator
      GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
      GIT_TAG        v3.2.1
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
endif()

add_subdirectory(src)

add_library(vucol SHARED ${SOURCES})
add_library(VUCOL::vucol ALIAS vucol)
set_target_properties(vucol PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_compile_features(vucol PUBLIC cxx_std_20)
target_include_directories(vucol
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(vucol
    PUBLIC
        Vulkan::Vulkan
    PRIVATE
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
        ${VUCOL_SHADERC_TARGET}
)

set(VUCOL_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/VUCOL")

install(TARGETS vucol
    EXPORT VUCOLTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/vucol"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp"
)

install(EXPORT VUCOLTargets
    FILE VUCOLTargets.cmake
    NAMESPACE VUCOL::
    DESTINATION "${VUCOL_INSTALL_CMAKEDIR}"
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/VUCOLConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VUCOLConfig.cmake"
    INSTALL_DESTINATION "${VUCOL_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/VUCOLConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VUCOLConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/VUCOLConfigVersion.cmake"
    DESTINATION "${VUCOL_INSTALL_CMAKEDIR}"
)

if(VUCOL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(VUCOL_BUILD_TESTS)
    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()
    enable_testing()

    set(INSTALL_GTEST OFF CACHE BOOL "Do not install VUCOL's GoogleTest dependency" FORCE)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)

    add_executable(vucol_tests ${TEST_SOURCES})
    target_link_libraries(vucol_tests PRIVATE vucol GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(vucol_tests)
endif()
