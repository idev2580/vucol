# Development Prompt 000

## User Request

Develop a very lightweight and simple compute library using Vulkan.

The API should resemble OpenGL's compute shader API, but avoid OpenGL's global
state-machine design. The library should be object-oriented while keeping the
workflow familiar for OpenGL compute users.

Update: the implementation should not manually code every low-level Vulkan
resource-management detail when a suitable helper library exists. The
implementation direction should prefer small, proven third-party libraries such
as Vulkan Memory Allocator (VMA) for memory allocation, and other lightweight
Vulkan helper libraries where they reduce complexity without bloating the public
API.

Update: use `vk-bootstrap` for Vulkan context/device/queue setup and VMA for
buffer allocation and memory management. Remove hand-written low-level context
selection and memory-type allocation code. Aggressively prefer `vulkan.hpp`
types and calls over the C Vulkan header/API in SOCL's own code.

## What To Implement

- C++ headers and source files for a small Vulkan compute wrapper.
- Add comments around Vulkan concepts that are easy to confuse in the SOCL API,
  especially allocation/memory concepts versus shader-binding concepts.
- Core objects should cover:
  - GPU/context selection and Vulkan device setup.
  - Buffer creation and memory management.
  - Descriptor set creation and buffer binding.
  - Compute shader pipeline creation from SPIR-V bytecode.
  - Synchronous and asynchronous compute dispatch.
- The API should support reusable pipelines, where the same pipeline can be used
  with different descriptor sets by rebinding.
- Buffer memory selection should be automatic:
  - Prefer host-visible memory on integrated GPUs.
  - Prefer device-local memory on discrete GPUs.
- Use helper libraries for repetitive Vulkan infrastructure when appropriate,
  especially VMA for buffer allocation and memory management.
- Use `vk-bootstrap` for:
  - instance creation,
  - physical device selection,
  - logical device creation,
  - compute queue lookup.
- Use `vulkan.hpp` in public/internal SOCL code where practical.

## How To Implement

- First inspect the existing project layout, CMake files, headers, sources, and
  tests.
- Keep the implementation lightweight and close to the current codebase style.
- Prefer RAII-style C++ ownership for Vulkan handles.
- Avoid manually implementing complex Vulkan helper behavior when a small,
  established third-party library can do it better.
- Before adding or integrating any third-party dependency, ask the user which
  dependency is acceptable and get permission to modify non-`prompts` files.
- Keep dependency choices conservative:
  - VMA is the preferred candidate for buffer and memory allocation.
  - `vk-bootstrap` is the preferred candidate for context/device/queue setup.
  - Other helper libraries may be considered for shader reflection, descriptor
    layout generation, or Vulkan bootstrap, but only if they materially simplify
    the code.
- For this implementation pass, explicitly refactor away manual physical-device
  enumeration, queue-family scanning, and memory-type selection.
- Prefer `vk::` handles, create-info structs, and member functions from
  `vulkan.hpp`; use C Vulkan handles only at interop boundaries required by VMA
  or `vk-bootstrap`.
- Keep comments concise and explanatory. Focus on public API concepts rather
  than restating what each line of code does.
- Avoid introducing unnecessary abstractions or large dependencies.
- After this prompt file is written, ask the user for permission before changing
  any non-`prompts` files, as required by `AGENTS.md`.
