cmake_minimum_required(VERSION 3.23)
project(socl_package_consumer LANGUAGES CXX)

find_package(SOCL 0.0.10 CONFIG REQUIRED)

add_executable(socl_package_consumer main.cpp)
target_link_libraries(socl_package_consumer PRIVATE SOCL::socl)
