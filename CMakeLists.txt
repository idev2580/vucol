cmake_minimum_required(VERSION 3.14)
project(socl)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SOCL_IS_TOP_LEVEL ON)
else()
    set(SOCL_IS_TOP_LEVEL OFF)
endif()

option(SOCL_BUILD_TESTS "Build SOCL tests" ${SOCL_IS_TOP_LEVEL})

if(SOCL_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(FetchContent)
find_package(Vulkan REQUIRED)

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
target_compile_features(socl PUBLIC cxx_std_20)
target_include_directories(socl
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)
target_link_libraries(socl PUBLIC Vulkan::Vulkan vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator)

if(SOCL_BUILD_TESTS)
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
