# Development Request Summary

## User request

Implement the planned Vulkan GPU execution-time measurement feature, and make any device-feature activation required for timing automatic during `Context` creation.

## What to implement

- Add opt-in GPU timing for one complete `beginTimed() -> submitAsync()` command batch.
- Add public capability information and a `supportsGpuTiming()` convenience query.
- Make `Context` query and automatically enable Vulkan 1.3 `synchronization2` when supported; callers must not configure it manually.
- Keep ordinary contexts and untimed dispatch batches usable when synchronization2 or queue timestamps are unsupported.
- Let `DispatchToken` wait for a timed submission and return its GPU duration.
- Retain timing query resources until GPU completion and release them with the existing batch resources.
- Document the timed synchronous/asynchronous usage and measured scope.
- Add focused API tests, including timestamp wraparound conversion where practical.

## How to implement

- Define a public floating-point nanosecond `GpuDuration` and `GpuTimingSupportInfo` in `Context.hpp`.
- Add `Context::beginTimed()`, `Context::supportsGpuTiming()`, and `Context::gpuTimingSupportInfo()`.
- Add `DispatchToken::waitAndGetGpuDuration()` while preserving the behavior of existing `wait()`.
- During physical-device setup, query `VkPhysicalDeviceSynchronization2Features`, enable it automatically when available, and record whether it was enabled.
- Consider timing supported only when synchronization2 is enabled, the chosen compute queue reports non-zero `timestampValidBits`, and `timestampPeriod` is positive.
- Give every timed batch its own two-slot `VK_QUERY_TYPE_TIMESTAMP` query pool.
- Record query reset and a `vkCmdWriteTimestamp2` at `TOP_OF_PIPE` when timed recording begins, then a second timestamp at `BOTTOM_OF_PIPE` immediately before ending the command buffer.
- After the submission fence completes, retrieve 64-bit query results, apply the queue's valid-bit mask to modular subtraction, multiply ticks by `timestampPeriod`, and cache the result on the token before releasing resources.
- Keep untimed batches free of query-pool allocation and timestamp commands.
- Do not compile or execute code in the agent environment, per project instructions.
