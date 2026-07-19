# Prompt 005: Buffer Staging Copy

## What to implement

Implement staging-copy support for `socl::Buffer::read()` and `socl::Buffer::write()` so CPU access works even when the buffer is allocated as device-local memory.

The policy should be:

- If the selected GPU is an integrated GPU with UMA, do not use staging buffers; use host-visible buffers directly.
- In all other cases, use staging buffers for CPU-to-GPU and GPU-to-CPU transfers.
- User-facing buffer access should remain simple: callers should keep using `buffer.write(...)` and `buffer.read(...)`.

## How to implement

- Detect UMA from the physical device memory properties during `Context` creation.
- Store whether a `Context` can use direct host-visible buffers without staging.
- Resolve `BufferType::Auto` to `HostVisible` only for integrated UMA GPUs; otherwise resolve to `DeviceLocal`.
- Keep `HostVisible` buffers directly mappable.
- For non-host-visible buffers, implement `Buffer::write()` with an internal host-visible staging buffer followed by `vkCmdCopyBuffer` into the destination buffer.
- For non-host-visible buffers, implement `Buffer::read()` with `vkCmdCopyBuffer` from the source buffer into an internal host-visible staging buffer followed by CPU readback.
- Add proper VMA flush/invalidate handling around mapped CPU writes/reads.
