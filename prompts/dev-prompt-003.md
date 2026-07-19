# Prompt 003: Uniform Buffer Example

## User request

Modify the example to include uniform buffer usage.

## What to implement

- Update the example compute workflow so it demonstrates passing small read-only parameter data through a uniform buffer.
- Keep the example focused on the existing elementwise add use case while showing how a uniform buffer is created, populated, bound, and consumed by the shader.

## How to implement

- Inspect the current example, descriptor set, buffer, and shader pipeline APIs.
- Prefer the existing SOCL API patterns and keep changes scoped to example files unless the current API cannot express uniform buffer usage.
- If non-`prompts` files must be changed, request user approval before editing them as required by `AGENTS.md`.
