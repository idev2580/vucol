# Investigate shaderc discovery failure

## User prompt

The user wants to find out why shaderc is not found by the project build configuration.

## What to implement

No code changes are requested yet. Investigate the existing dependency discovery logic and identify why CMake may fail to find shaderc.

## How to implement

- Inspect the top-level CMake configuration and shader compiler source usage.
- Check whether shaderc discovery depends on a CMake config package, Vulkan SDK layout, platform-specific fallbacks, or target naming.
- Do not run compilation or project execution commands in the agent environment.
- Report the likely failure cause and concrete diagnostics or fixes for the developer to run locally.
