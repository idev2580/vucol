# Development Request Summary

## User request

Raise SOCL's Vulkan API requirement from Vulkan 1.1 to Vulkan 1.3, and prepare a concrete follow-up implementation plan for GPU execution-time measurement.

## What to implement

- Require Vulkan 1.3 headers and loader support at CMake configuration time.
- Raise the minimum CMake version to 3.23 so `FindVulkan` can report and enforce the discovered Vulkan header version.
- Request a Vulkan 1.3 instance and select only physical devices supporting Vulkan 1.3.
- Report Vulkan 1.3 to Vulkan Memory Allocator during allocator creation.
- Change SOCL's explicit default shader-compilation target from Vulkan 1.1 to Vulkan 1.3.
- Update the corresponding API test expectation.
- Design, but do not yet implement, a lightweight GPU-timing API and internal lifecycle that fits synchronous and asynchronous command batches.

## How to implement

- Change the minimum CMake version from 3.14 to 3.23 and change
  `find_package(Vulkan REQUIRED)` to `find_package(Vulkan 1.3 REQUIRED)`.
- Change vk-bootstrap instance and physical-device minimum versions from 1.1 to 1.3.
- Change `VmaAllocatorCreateInfo::vulkanApiVersion` to `VK_API_VERSION_1_3`.
- Keep the older `VulkanVersion` enum values available for callers that intentionally compile shaders for older target environments; only change the default option.
- Do not enable unrelated Vulkan 1.3 optional features as part of the version-requirement change.
- Update focused source/header/test files only; do not perform unrelated refactoring.
- In the timing plan, use Vulkan 1.3 timestamp queries and account for feature/support checks, query ownership, command placement, asynchronous result retrieval, tick wraparound, errors, documentation, and focused tests.
- Do not compile or execute code in the agent environment, per project instructions.
