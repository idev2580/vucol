# Development Request Summary

## User request

Add an English `Tutorial.md` Doxygen page showing the simplest SOCL implementation of AXPY and then extending it to record multiple AXPY commands for one submission.

## What to implement

- Add `docs/Tutorial.md` as a Doxygen-visible tutorial page.
- Explain the AXPY operation `y = a * x + y` and provide a minimal GLSL compute shader.
- Show the matching SOCL pipeline, descriptor bindings, buffers, push constants, dispatch, asynchronous submission, wait, and result readback.
- Show how to record several AXPY dispatches in one `begin() -> submitAsync()` batch.
- Demonstrate descriptor snapshot behavior by rebinding the reusable logical descriptor set between AXPY dispatches.
- Explain buffer-access declarations and automatic barriers when multiple AXPY operations share data.
- Link the tutorial to the dispatch snapshot guide and make it discoverable from relevant public API documentation.

## How to implement

- Use only API forms present in the current public headers and existing examples.
- Keep the GLSL source inside the C++ example and compile it in-process with SOCL's shaderc-backed `compileGlslToSpirv()` API.
- Make the tutorial a self-contained C++ example with no external shader file or shader compiler command.
- Keep the first example straightforward, then reuse its objects in the batched example instead of introducing an unnecessary framework.
- Clearly distinguish batching several dispatches in one command buffer from submitting independent asynchronous batches.
- Update the Doxygen input only if needed; `docs` is already included.
- Do not compile, execute, or generate documentation in the agent environment, per project instructions.
