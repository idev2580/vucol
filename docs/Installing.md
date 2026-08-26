# Installing and consuming SOCL

SOCL installs a CMake config package that exports the `SOCL::socl` target.
Vulkan 1.3 or newer is the only public package dependency. VMA, vk-bootstrap,
and shaderc remain implementation dependencies of the SOCL shared library.

Configure, build, and install SOCL into a prefix:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/socl-prefix \
  -DSOCL_BUILD_TESTS=OFF \
  -DSOCL_BUILD_EXAMPLES=OFF \
  -DSOCL_BUILD_DOCS=OFF
cmake --build build
cmake --install build
```

Consume the installed package from another CMake project:

```cmake
cmake_minimum_required(VERSION 3.23)
project(my_compute_project LANGUAGES CXX)

find_package(SOCL CONFIG REQUIRED)

add_executable(my_compute_app main.cpp)
target_link_libraries(my_compute_app PRIVATE SOCL::socl)
```

If SOCL is installed outside the platform's normal package search paths, pass
its prefix while configuring the consumer:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/socl-prefix
cmake --build build
```

The runtime loader must also be able to locate the installed SOCL shared
library and any shared private dependencies, such as shaderc when it is linked
dynamically. Package managers normally arrange this automatically; custom
prefixes may require the platform's runtime library search path to be adjusted.
