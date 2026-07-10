# Development Prompt 001

## User Request

Improve the CMake setup so SOCL can be consumed from another project using
`FetchContent`.

## What To Implement

- Make the top-level `CMakeLists.txt` friendly when SOCL is added as a
  subproject through `FetchContent_MakeAvailable`.
- Keep SOCL's library target usable by consumers without relying on global
  include directories or source paths tied to the parent project's
  `CMAKE_SOURCE_DIR`.
- Avoid building SOCL tests automatically when SOCL is included by another
  project, while still allowing tests when SOCL is configured as the top-level
  project.
- Keep existing dependency behavior for Vulkan, `vk-bootstrap`, VMA, and
  GoogleTest where appropriate.

## How To Implement

- Inspect existing CMake files first.
- Replace root-relative path usage with `PROJECT_SOURCE_DIR` or
  `CMAKE_CURRENT_SOURCE_DIR` so paths remain correct under `FetchContent`.
- Gate tests behind an option that defaults on only for the top-level project.
- Use target-scoped include directories and compile features instead of global
  CMake settings where practical.
- Keep the resulting CMake small and consistent with the current project.
- Ask the user for permission before changing any non-`prompts` files, as
  required by `AGENTS.md`.
