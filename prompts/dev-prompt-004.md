# Prompt 004: Typed Specialization Constant Convenience API

## User request

Add a convenience API for specialization constants so users can provide values other than `std::uint32_t`.

## What to implement

- Extend the specialization constant API beyond the current `std::uint32_t`-only representation.
- Provide an ergonomic helper such as `socl::specConstant(value)` for scalar values like `uint32_t`, `int32_t`, `float`, and `bool`.
- Preserve simple pipeline creation syntax and compatibility with existing `{id, uint32_t}` style where practical.
- Update `elementwise_add.cpp` with no-op example entries that demonstrate multiple specialization constant value types, with a comment making clear they exist only to show API usage.

## How to implement

- Inspect the current `ShaderPipelineCreateInfo` and `Context::createShaderPipeline` implementation.
- Store specialization constant payloads as raw bytes with per-constant sizes.
- Build Vulkan `vk::SpecializationMapEntry` offsets and sizes from the stored byte payloads.
- Add focused API tests for typed specialization constants.
- Add example `specConstants` entries for unsigned integer, signed integer, float, and bool values.
- Request approval before modifying non-`prompts` files, as required by `AGENTS.md`.
