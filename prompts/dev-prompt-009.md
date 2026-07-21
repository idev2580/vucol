# Windows shaderc discovery in CMake

## User prompt

The project should compile on Windows, where the current shaderc fallback fails because it unconditionally requires pkg-config.

## What to implement

- Update the top-level CMake configuration so shaderc discovery never depends on pkg-config.
- Accept shaderc when its CMake config package provides one of the supported library targets.
- On Windows, fall back to shaderc headers and libraries shipped in the SDK referenced by `VULKAN_SDK`.
- On non-Windows platforms, treat a missing or unusable shaderc CMake package as a missing required library.
- Emit a clear configuration error when shaderc cannot be found.

## How to implement

- Use `find_package(shaderc CONFIG)` and recognize its known target names.
- Supply Windows-specific package hints for common CMake config locations beneath `%VULKAN_SDK%`.
- If the Windows Vulkan SDK does not provide a shaderc CMake config package, locate its include directory and `shaderc_combined` library with `find_path` and `find_library`, then expose them through an imported target. Handle release and debug libraries where available.
- Remove the pkg-config lookup entirely. Do not add a manual library fallback on non-Windows platforms.
- Stop configuration with a direct, actionable error if no supported shaderc CMake target exists.
- Do not compile or execute the project in the agent environment; leave build verification to the developer.
