# Add shaderc pkg-config fallback

## User prompt

The user wants to add a pkg-config fallback route for shaderc discovery because shaderc is installed on the system but does not provide a CMake config package.

## What to implement

Update the top-level CMake shaderc discovery logic so non-Windows systems can find shaderc through pkg-config when `find_package(shaderc CONFIG)` fails.

## How to implement

- Preserve the existing shaderc CMake config package lookup and supported target names.
- Preserve the existing Windows `VULKAN_SDK` fallback.
- On non-Windows platforms, call `find_package(PkgConfig QUIET)` and `pkg_check_modules(... IMPORTED_TARGET shaderc)`.
- Link SOCL against the imported pkg-config target when available.
- Improve the missing dependency error message so it mentions pkg-config as a valid non-Windows route.
- Do not run compile or execution commands in the agent environment.
