cmake_minimum_required(VERSION 3.23)
project(vucol_package_consumer LANGUAGES CXX)

find_package(VUCOL 0.0.10 CONFIG REQUIRED)

add_executable(vucol_package_consumer main.cpp)
target_link_libraries(vucol_package_consumer PRIVATE VUCOL::vucol)
