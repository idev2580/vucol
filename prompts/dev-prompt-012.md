# Add subgroup controls and shader target-version options

## User prompt

The user wants SOCL contexts to enable all applicable subgroup size-control features automatically when the selected GPU supports them. Users should be able to select a required subgroup size per shader pipeline, while omission uses the GPU's default subgroup size.

The original request also requires shader compilation options for selecting the target Vulkan and SPIR-V versions.

## What to implement

- Query and expose the selected GPU's core subgroup properties.
- Query `VK_EXT_subgroup_size_control` properties and features when available.
- During Context initialization, automatically enable every supported subgroup size-control feature:
  - `subgroupSizeControl`
  - `computeFullSubgroups`
- Let users inspect both support and actual enablement of `computeFullSubgroups` through the public subgroup support information.
- Add an optional required subgroup size to `ShaderPipelineCreateInfo`.
- Use the GPU-selected default subgroup size when the pipeline option is omitted.
- Reject an explicitly requested subgroup size with a clear error when size control is unavailable or the requested size is unsupported.
- Add shader compilation options for selecting Vulkan 1.0 through 1.3 and SPIR-V 1.0 through 1.6 targets.
- Preserve the existing shader compilation function and its behavior.

## How to implement

- Add a lightweight `SubgroupSupportInfo` public structure containing the core subgroup size, supported shader stages and operations, plus size-control support, `computeFullSubgroups` support, their actual enabled states, and the reported minimum and maximum subgroup sizes.
- Store the queried subgroup information in `ContextState` and expose it through a const Context accessor.
- Do not treat core subgroup operations as device features that can be enabled; they are capabilities reported through `VkPhysicalDeviceSubgroupProperties`.
- Add `std::optional<std::uint32_t> requiredSubgroupSize` to `ShaderPipelineCreateInfo`, defaulting to no value.
- Chain `VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT` into the compute shader stage only when a subgroup size was explicitly requested.
- Validate an explicit size against extension/feature support, the reported range, power-of-two requirements, and compute-stage support before pipeline creation.
- Preserve existing Context and shader-pipeline behavior when no subgroup size is requested.
- Add a shader compiler overload that accepts the new options without breaking existing source-name calls, and translate those options to shaderc target environment and target SPIR-V settings.
- Add focused API/validation tests without broad refactoring.
- Avoid vk-bootstrap's deprecated selector-level desired-extension API; enable the subgroup size-control extension on the selected `vkb::PhysicalDevice` with `enable_extension_if_present` instead.
- Keep the shader compile-options API test outside the existing GLSL raw string so the original shader bytecode test remains valid.
- Do not run compile or execution commands in the agent environment.
