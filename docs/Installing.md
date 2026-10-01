# Installing and consuming VUCOL

VUCOL installs a CMake config package that exports the `VUCOL::vucol` target.
Vulkan 1.3 or newer is the only public package dependency. VMA, vk-bootstrap,
and shaderc remain implementation dependencies of the VUCOL shared library.

Configure, build, and install VUCOL into a prefix:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/vucol-prefix \
  -DVUCOL_BUILD_TESTS=OFF \
  -DVUCOL_BUILD_EXAMPLES=OFF \
  -DVUCOL_BUILD_DOCS=OFF
cmake --build build
cmake --install build
```

Consume the installed package from another CMake project:

```cmake
cmake_minimum_required(VERSION 3.23)
project(my_compute_project LANGUAGES CXX)

find_package(VUCOL CONFIG REQUIRED)

add_executable(my_compute_app main.cpp)
target_link_libraries(my_compute_app PRIVATE VUCOL::vucol)
```

If VUCOL is installed outside the platform's normal package search paths, pass
its prefix while configuring the consumer:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/vucol-prefix
cmake --build build
```

The runtime loader must also be able to locate the installed VUCOL shared
library and any shared private dependencies, such as shaderc when it is linked
dynamically. Package managers normally arrange this automatically; custom
prefixes may require the platform's runtime library search path to be adjusted.
