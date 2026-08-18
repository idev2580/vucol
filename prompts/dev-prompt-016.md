# Adopt Doxygen for Public API Documentation

## User request

Adopt Doxygen comments for every public API. Document all public classes and their methods, including at minimum:

1. Input arguments, output arguments, and return values for every method.
2. Thread-safety behavior for most methods.
3. Synchronous or asynchronous behavior for control-related methods where relevant.
4. Safety and lifetime-control mechanisms for GPU resources.

After the initial implementation, the user reported that the documentation
target does not build. The task therefore also includes reproducing and fixing
the Doxygen configuration/generation failure rather than relying only on static
inspection.

The user additionally requested that the relationship between `dispatch()` and
`submitAsync()` be emphasized: each `dispatch()` snapshots the current buffers
and records that snapshot's descriptor bind plus dispatch command, while
`submitAsync()` submits the already completed command sequence once. The GPU then
executes the recorded per-dispatch buffer changes in order without CPU rebinding.

## What to implement

- Add Doxygen-compatible descriptions to every public class and public method in the library headers.
- Describe parameters and return values explicitly, including constructors and methods returning `void` where useful.
- State concurrency guarantees or restrictions for APIs where thread-safety matters.
- State whether dispatch, synchronization, compilation, and other control operations complete synchronously or asynchronously.
- Explain ownership, lifetime, in-flight-use, bounds, mapping, and destruction safeguards for Vulkan/GPU resources.
- Clearly distinguish per-dispatch descriptor snapshot/command recording from the
  single asynchronous queue submission performed by `submitAsync()`.
- Add an opt-in CMake target that generates the public API reference with Doxygen
  and treats incomplete or invalid documentation as an error.

## How to implement

- Inspect declarations in `include/socl/` and their implementations in `src/` so the comments reflect actual behavior rather than assumptions.
- Use standard Doxygen commands such as `@brief`, `@param`, `@return`, `@note`, `@warning`, and `@throws` where applicable.
- Keep documentation on public declarations in headers, with terminology and thread-safety wording consistent across classes.
- Add an `SOCL_BUILD_DOCS` CMake option and a `socl_docs` target based on
  CMake's `FindDoxygen` integration, scanning only `include/socl/` and excluding
  internal `socl::detail` symbols.
- Add an `SOCL_DOCS_ONLY` configuration path so generating API documentation does
  not require Vulkan, shaderc, project compilation, or network-backed FetchContent
  dependencies.
- Change source/header files only after receiving the user's explicit permission, as required by `AGENTS.md`.
- Do not compile or run the library or tests. The user explicitly permits running
  the Doxygen-related configuration/generation action to validate documentation.
- Consider the work complete only after the documentation target itself generates
  successfully, or report a concrete environmental dependency that prevents it.

## Validation result

- Reproduced the original documentation configuration failure: the normal project
  path attempted to download `vk-bootstrap` before reaching Doxygen.
- Added the dependency-free `SOCL_DOCS_ONLY` path.
- Configured with Doxygen 1.17.0 and built `socl_docs` successfully with warnings
  treated as errors; the generated HTML entry point was verified to exist.
