# SOCL
Simple Open Compute Library based on Vulkan.

## Main Purpose
Aim to be a simple wrapper that enables developer to write GPGPU code as OpenGL-style compute code.
The user's work should only be as below.

1. Create context for specific GPU
2. Create buffers and descriptor sets using context
3. Create shader pipeline from SPIR-V bytecodes
4. Binding buffers to shader pipeline
5. Dispatch workgroups(Both sync and async should be possible)

### Key features

- Automatic buffer type selection: iGPU prefers host visible, dGPU prefers device local
- Reusable pipeline: For same operation, only re-binding to other descriptor sets should work.
- Very simple codebase: vkc should be light as possible.

## Conventions
- Before do actual development, always write and summarize the user prompt into prompts directory as a markdown file, named `dev-prompt-XXX.md` where 'XX' means number. Count the number from 0.
    - Summary might be changed during the conversation. Summary should contain 'what to implement' and 'how to implement'
- Except for `prompts` directory, if you want to change any file, you must ask the user to allow that change.