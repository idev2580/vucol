# Development Prompt 002

## User Request

1. Extend the SOCL API so users can select the GPU when creating a context.
2. Add examples that show full API usage from context creation through performing operations.
3. Include an element-wise operation example.
4. Add logic for printing the selected context GPU name and information.
5. Add logic for checking Vulkan device extension support.

## What To Implement

- Add context creation support for selecting a specific GPU/physical device.
- Preserve the existing simple context creation path if possible.
- Expose selected GPU information such as name, type, vendor ID, device ID,
  API version, driver version, and queue family.
- Provide a way to print selected GPU information from a context.
- Provide Vulkan device extension checks and expose supported extension names.
- Add example code that demonstrates the intended full workflow:
  - create/select a GPU context
  - print selected GPU information
  - check Vulkan extension availability
  - create buffers and descriptor sets
  - create a shader pipeline from SPIR-V bytecode
  - bind buffers to the pipeline
  - dispatch workgroups
  - read or verify element-wise operation results

## How To Implement

- Inspect the existing `Context`, buffer, descriptor set, shader pipeline, CMake, and test structure.
- Design the GPU selection API to match the current lightweight style.
- Add lightweight data structs and methods to `Context` rather than introducing
  heavyweight device manager abstractions.
- Use vk-bootstrap as much as possible for physical-device selection, required
  extension filtering, logical-device creation, and queue lookup.
- Select the requested physical device from vk-bootstrap's suitable device list
  during context construction, and keep the existing default constructor behavior.
- Request user approval before modifying files outside the `prompts` directory.
- After approval, update headers/sources and add examples with build integration.
- Run available build/tests if dependencies and environment permit.
