# Dispatch Snapshots and Batched Submission {#dispatch_snapshots}

SOCL records a complete sequence of compute commands between
`socl::Context::begin()` and `socl::Context::submitAsync()`. Each call to
`socl::Context::dispatch()` freezes the descriptor bindings needed by that
dispatch. A later pipeline selection or descriptor rebinding therefore does not
change a dispatch that has already been recorded.

For a complete working example, see @ref tutorial_axpy.

## What is captured for a dispatch

The following state is preserved for every recorded dispatch:

- **Descriptor bindings.** SOCL copies the current logical `socl::DescriptorSet`
  bindings into a newly allocated native Vulkan descriptor set. The snapshot
  contains the bound buffers, their ranges, descriptor types, binding numbers,
  and declared `socl::BufferAccess` modes. Rebinding the logical descriptor set
  afterward does not modify this native snapshot. A pipeline with no descriptor
  bindings needs no descriptor snapshot.
- **Shader pipeline.** `socl::Context::use()` records a Vulkan pipeline-bind
  command immediately. Each dispatch retains the selected pipeline state until
  its batch completes. The pipeline object is not cloned; the recorded bind
  command and retained immutable pipeline state preserve the selection.
- **Pipeline/descriptor compatibility.** A descriptor snapshot is allocated
  from the descriptor-set layout belonging to its selected pipeline and retains
  that pipeline state.
- **Workgroup counts.** The `groupCountX`, `groupCountY`, and `groupCountZ`
  arguments are encoded directly into the recorded dispatch command.
- **Buffer lifetime and access metadata.** Captured buffers are retained until
  the batch completes. SOCL merges access modes for repeated use in a batch,
  prevents conflicting CPU or other-batch access, and records compute-to-compute
  buffer barriers between dispatches when either adjacent use writes.

Push constants require one distinction: `socl::Context::push()` copies its bytes
directly into a Vulkan push-constant command when `push()` is called. They are
therefore preserved in command order, but they are not stored inside the native
descriptor snapshot. Push-constant state remains in effect until another push
command for the applicable range changes it. After selecting another pipeline,
push the values required by that pipeline explicitly; SOCL does not create a
separate implicit push-constant snapshot at `dispatch()`.

`submitAsync()` does not rebuild snapshots or revisit the current C++ objects.
It ends and submits the command buffer that already contains the ordered
pipeline binds, push-constant updates, descriptor-snapshot binds, barriers, and
dispatch commands.

## Changing state between dispatches

It is safe to change the pipeline and bindings after recording one dispatch and
then record another dispatch in the same batch. For example:

```cpp
context.begin();

context.use(pipelineA);
context.bind(setA);
context.push(constantsA);
context.dispatch(groupsA);

context.use(pipelineB);
context.bind(setB);
context.push(constantsB);
context.dispatch(groupsB);

auto token = context.submitAsync();
```

The first dispatch continues to use `pipelineA`, `setA`'s captured bindings, and
`constantsA`. The second uses `pipelineB`, `setB`'s captured bindings, and
`constantsB`.

Selecting a different pipeline clears the currently selected descriptor set when
that set belongs to the previous pipeline. Call `use(pipelineB)` before
`bind(setB)`. Calling `bind(setB)` while an incompatible pipeline is still
selected is an error; automatic pipeline selection by `bind()` occurs only when
no pipeline is currently selected.

It is also safe to rebind buffers in the same logical descriptor set after a
dispatch and use it for a later dispatch:

```cpp
context.bind(set);
context.dispatch(firstGroups);       // captures the current bindings

set.bindBuffer(0, anotherBuffer, socl::BufferAccess::Read);
context.bind(set);
context.dispatch(secondGroups);      // captures the new bindings
```

Changing a descriptor binding is different from changing the contents of a
captured buffer. Buffer reads and writes remain subject to SOCL's recorded and
in-flight access checks until the owning batch completes.

## Recording lifecycle and execution plans

`begin()` is required once for every command batch, not once for the lifetime of
a context and not once per operator. The valid lifecycle is:

```text
begin -> record one or more operators -> submitAsync
```

After `submitAsync()`, that recording is closed. SOCL clears the context's
current pipeline and descriptor-set selection, and the returned
`socl::DispatchToken` takes ownership of the submitted one-time command buffer.
A later call to `use()`, `bind()`, `push()`, or `dispatch()` therefore requires a
new `begin()`. Calling `submitAsync()` again without first beginning and
recording a new batch is also an error.

For a soclBLAS-style execution plan, store the logical operations and their
pipeline, binding, constant, and dispatch parameters in the plan. Replay those
operations into a fresh SOCL recording each time the plan executes:

```cpp
socl::DispatchToken ExecutionPlan::execute(socl::Context& context) const {
    context.begin();

    for (const auto& operation : operations_) {
        operation.record(context);  // use, bind, push, and dispatch as needed
    }

    return context.submitAsync();
}
```

The current SOCL API does not preserve a finished command buffer as a reusable
execution-plan object. In particular, this is not supported:

```cpp
context.begin();
plan.record(context);
auto first = context.submitAsync();
auto second = context.submitAsync(); // error: no active recording
```

If plans or operators must be composable into a larger batch, separate recording
from submission. A `record(context)` function should assume that its caller has
already called `begin()` and should emit only `use()`, `bind()`, `push()`, and
`dispatch()` calls. The outer owner of the complete batch calls `begin()` and
`submitAsync()` exactly once:

```cpp
context.begin();
planA.record(context);
planB.record(context);
auto token = context.submitAsync();
```

Do not put `begin()` and `submitAsync()` inside every operator. Doing so creates
separate submissions and prevents SOCL from recording the whole operator graph
as one ordered batch with automatic inter-dispatch barriers.

After one asynchronous execution has been submitted, another batch may be begun
on the same context while the first token is still valid. This is useful for
non-conflicting resources. If the next execution claims a buffer in a way that
conflicts with the recorded or in-flight batch, SOCL rejects the dispatch. Wait
for the earlier token before replaying the plan with those buffers.

## One batch versus separate submissions

Consider these two logical sequences:

```text
dispatch A -> submit/wait -> dispatch B -> submit
dispatch A -> dispatch B -> submit
```

They produce the same computed values when all of the following are true:

- A and B use the same pipelines, descriptor bindings, push constants,
  workgroup counts, and initial buffer contents in both versions.
- The shaders are deterministic for those inputs.
- Every buffer access is declared accurately with `socl::BufferAccess`.
- No host operation, other queue work, or external synchronization changes or
  observes relevant state at the boundary between A and B.
- In the separate-submission version, A is complete before conflicting buffers
  are reused by B.

The two forms do not have identical synchronization behavior:

- In one batch, SOCL records both dispatches in one command buffer and inserts
  the required compute-to-compute buffer barriers from their declared access
  modes. The GPU executes the recorded commands in order after one submission.
- Separate submissions create separate command buffers, fences, resource
  trackers, and submission boundaries. For conflicting buffer use, wait for the
  first `socl::DispatchToken` (or use `socl::Context::submitAndWait()`) before
  recording the second dispatch. Otherwise SOCL rejects the conflicting buffer
  claim while the first batch remains recorded or in flight.
- A wait between submissions permits host-side reads, writes, or decisions and
  introduces a CPU/GPU synchronization point. A single batch has no such host
  intervention between its dispatches.
- Disjoint read-only or otherwise non-conflicting work may be submitted without
  waiting, but this is not equivalent to adding a completion boundary between
  the dispatches.

Use a single batch when the operations form a fixed GPU-side sequence. Use
separate submissions when the host must observe completion, access results, or
decide what to submit next.

## Lifetime

The `socl::DispatchToken` returned by `submitAsync()` owns the submitted command
buffer and fence and retains all captured descriptor snapshots, pipelines, and
buffers. `socl::DispatchToken::wait()`, token destruction, or replacement of a
valid token waits for GPU completion before those retained resources are
released.
