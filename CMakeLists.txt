cmake_minimum_required(VERSION 3.14)
project(socl)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(FetchContent)
enable_testing()
find_package(Vulkan REQUIRED)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
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
FetchContent_MakeAvailable(googletest)
FetchContent_MakeAvailable(vk-bootstrap VulkanMemoryAllocator)
set_property(TARGET vk-bootstrap PROPERTY POSITION_INDEPENDENT_CODE ON)
add_subdirectory(src)
add_subdirectory(tests)

include_directories("${CMAKE_SOURCE_DIR}/include")
add_library(socl SHARED ${SOURCES})
target_include_directories(socl PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(socl PUBLIC Vulkan::Vulkan vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator)

if(TEST_SOURCES)
    add_executable(socl_tests ${TEST_SOURCES})
    target_link_libraries(socl_tests PRIVATE socl GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(socl_tests)
endif()
