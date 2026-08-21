# Development Request Summary

## User request

Investigate ways to add GPU execution-time measurement to SOCL before choosing or implementing an API.

## What to implement

- Research Vulkan mechanisms that can measure compute work on the GPU.
- Compare their accuracy, portability, synchronization cost, and fit with SOCL's synchronous and asynchronous dispatch APIs.
- Inspect the current SOCL recording, submission, and `DispatchToken` lifetime model to identify practical integration points.
- Recommend a small public API and internal design direction, without changing implementation files yet.

## How to implement

- Base Vulkan behavior and requirements on current official Khronos documentation.
- Treat timestamp queries around compute dispatches or submitted command batches as the primary candidate, and evaluate host-side timing and profiling tools as alternatives.
- Account for timestamp support, `timestampPeriod`, valid timestamp bits, wraparound, query-result availability, synchronization, and asynchronous result collection.
- Preserve SOCL's lightweight design and reusable pipeline/descriptor model.
- Do not modify files outside `prompts` during the research phase.
- Do not compile or execute code in the agent environment, per project instructions.
