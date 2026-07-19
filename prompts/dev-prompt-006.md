# Prompt 006: Rename Auto Buffer Policy Flag

## What to implement

Rename the internal `ContextState::useHostVisibleAutoBuffers` field to make it clear that it is not a user-facing option.

## How to implement

- Change the field name to `autoBufferUsesHostVisibleMemory`.
- Update all references in `Context.cpp`.
- Do not change public API or buffer behavior.
