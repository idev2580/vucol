# Cooperative Matrix Support API

## User prompt

The user needs an API for checking Vulkan Cooperative Matrix Extension support. The main requirement is to determine which cooperative matrix tile combinations are supported by the selected GPU/device.

## What to implement

- Add a public SOCL API that reports whether cooperative matrix support is available for the current Vulkan context/device.
- Expose the supported cooperative matrix tile/type combinations so callers can choose a valid tile shape for shaders.
- Keep the API simple and aligned with the existing SOCL context-centric workflow.

## How to implement

- Inspect existing context/device initialization and public header patterns before editing non-prompt files.
- Use Vulkan cooperative matrix property queries from the selected physical device, gated by extension/property availability.
- Store or expose results through lightweight C++ data structures rather than requiring users to call raw Vulkan queries.
- Add focused tests or examples if the current project structure can support them without excessive setup.

## Implemented API

- `socl::CooperativeMatrixTileProperties`
- `socl::CooperativeMatrixSupportInfo`
- `socl::Context::supportsCooperativeMatrix()`
- `socl::Context::cooperativeMatrixSupportInfo()`
