# Prompt Summary

## What to implement

Add a helper function that compiles a GLSL shader source string into SPIR-V bytecode.
Keep it separate from shader pipeline creation because GLSL compilation is a source-level utility, not tightly coupled to pipeline state.

## How to implement

Prefer the simplest implementation using `shaderc`. Require a normal shaderc development package, include the official `shaderc/shaderc.hpp` header, and link through the normal CMake dependency path instead of redeclaring shaderc ABI functions manually. Add separate public header/source files, for example `include/socl/ShaderCompiler.hpp` and `src/ShaderCompiler.cpp`, with an API that returns `std::vector<std::uint32_t>` from a GLSL source string. Keep the helper aligned with the existing SOCL API style and verify the build/tests after the change.
