# Development Request Summary

## User request

Create a dedicated English Doxygen page that clearly documents SOCL's per-dispatch snapshot and batched submission model.

## What to implement

- Add a standalone Doxygen-visible document about dispatch snapshots.
- Explain exactly which state and resources are captured or recorded for each `Context::dispatch()`.
- Explain how shader-pipeline changes are recorded and how pipeline lifetimes are retained.
- Explain descriptor-set and buffer-binding snapshots, push constants, workgroup counts, buffer access tracking, and inter-dispatch barriers.
- Document the required call order when switching pipelines and descriptor sets.
- Compare `dispatch -> submit -> dispatch -> submit` with `dispatch -> dispatch -> submit`, including the conditions under which they produce the same computed values and the synchronization/lifetime differences between them.
- Explain the `begin() -> record operations -> submitAsync()` lifecycle for a soclBLAS-style `ExecutionPlan`, including repeated execution and composable plans.
- Link the guide from the relevant public API documentation if necessary so it is discoverable in generated Doxygen output.

## How to implement

- Write the English guide as `docs/dispatch-snapshots.md`, with an explicit Doxygen page anchor.
- Add the `docs` directory to the Doxygen input and link the guide from the relevant public API documentation.
- Base every claim on the current `Context::use()`, `bind()`, `push()`, `dispatch()`, and `submitAsync()` implementation.
- Clearly distinguish immutable descriptor snapshots from commands recorded directly into the Vulkan command buffer.
- State that multiple recorded dispatches execute in command-buffer order after one submission, while separate submissions have distinct completion/submission boundaries.
- Avoid claiming general equivalence where host access, other queue work, external synchronization, or submission-boundary behavior can make the two patterns differ.
- Clarify that the current SOCL API does not cache a recorded command buffer for repeated `submitAsync()` calls: every new batch execution requires a new `begin()` and re-recording pass.
- Do not compile, execute, or generate documentation in the agent environment, per project instructions.
