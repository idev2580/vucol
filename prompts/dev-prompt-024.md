# Development Request Summary

## User request

Package SOCL so it can be installed and consumed from another CMake project through installed `.cmake` package files.
Move VMA-backed state out of the public headers so only Vulkan remains a public dependency where practical.
For VMA and vk-bootstrap, prefer an already available package and use FetchContent only as a fallback.
Keep the existing FetchContent approach for GoogleTest.
Implement the remaining install and package-export work, then verify a separate consumer through the installed package.

## What to implement

- Add CMake install rules for the SOCL shared library and public headers.
- Export an installed CMake target with a stable namespace so consumers can use `find_package` and `target_link_libraries`.
- Generate and install SOCL package configuration and compatible version files.
- Make the installed target's include paths and transitive dependencies valid outside the source/build tree.
- Move VMA-backed internal state definitions out of public headers so VMA can be linked and included privately.
- Document and validate the intended install-and-consume workflow.

## How to implement

- Use standard GNU installation directories and generator expressions for separate build-tree and install-tree include paths.
- Install the target and public `include/socl` tree, and export the target as a namespaced imported target such as `SOCL::socl`.
- Generate `SOCLConfig.cmake` from a template with `configure_package_config_file`, and generate `SOCLConfigVersion.cmake` with `write_basic_package_version_file`.
- Resolve required transitive dependencies from the installed package configuration and decide explicitly how FetchContent dependencies are made available to downstream consumers.
- Keep only forward declarations of internal state in public headers, place `BufferState` and `ContextState` definitions in a non-installed internal header, and remove public `vk_mem_alloc.h` includes.
- Link Vulkan publicly because Vulkan types are part of SOCL's public API; link VMA, vk-bootstrap, and shaderc privately.
- Add a minimal external consumer fixture or documented smoke-test procedure that configures against an installation prefix.
- Keep tests, examples, and documentation disabled by default when SOCL is embedded or packaged as appropriate.
- Preserve the existing GoogleTest dependency flow.
- Configure, compile, and run tests in the agent environment to validate the changes, as explicitly authorized by the user.
