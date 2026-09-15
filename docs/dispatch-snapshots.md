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
  the batch completes. SOCL preserves each binding's byte offset, byte size, and
  access mode. It records compute-to-compute buffer barriers for overlapping
  ranges when either access writes.

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
captured buffer. Recording a batch does not itself add a CPU-access claim, so
CPU reads and writes remain allowed unless an earlier uncollected submission
already uses that buffer. `submitAsync()` claims every buffer used by the new
batch, and CPU access is rejected until every submitted claim has been collected
by waiting for or destroying its `DispatchToken`.

## Buffer ranges

The whole-buffer `bindBuffer()` overload remains available. A second overload
binds one byte range of the same allocation:

```cpp
const auto alignment = context.bufferOffsetAlignment(socl::DescriptorType::StorageBuffer);
const auto stride = (valueSize + alignment - 1) / alignment * alignment;
set.bindBuffer(0, storage, 0, valueSize, socl::BufferAccess::Read);
set.bindBuffer(1, storage, stride, valueSize, socl::BufferAccess::Write);
set.bindBuffer(2, storage, stride * 2, valueSize, socl::BufferAccess::Read);
```

Each dispatch snapshot retains these offsets and sizes. The shader sees offset
zero at the beginning of each bound range. A binding rejects empty or
out-of-bounds ranges, offsets that violate the device's storage/uniform buffer
alignment, and ranges larger than the corresponding descriptor limit. Use
`Context::bufferOffsetAlignment()` when laying out adjacent ranges.

GPU dependency tracking is range based. SOCL emits no barrier for disjoint
ranges or read-after-read. RAW, WAR, and WAW dependencies over intersecting
ranges receive a barrier covering their intersection. Access state is retained
independently for untouched portions of a buffer.

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
on the same context while the first token is still valid. Both submissions use
the Context's single queue and may reference the same buffers. The next batch's
dispatches compare their ranges with the last accesses submitted to that queue,
so SOCL records the required submission-boundary barriers without a CPU wait.

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
- The separate submissions use the same SOCL Context and therefore the same
  Vulkan queue.

The two forms do not have identical synchronization behavior:

- In one batch, SOCL records both dispatches in one command buffer and inserts
  the required compute-to-compute buffer barriers from their declared access
  modes. The GPU executes the recorded commands in order after one submission.
- Separate submissions create separate command buffers, fences, resource
  trackers, and submission boundaries. SOCL carries the last range access state
  across those boundaries and records dependencies in the later command buffer.
  Waiting between GPU submissions is not required, even when they share buffers.
- A wait between submissions permits host-side reads, writes, or decisions and
  introduces a CPU/GPU synchronization point. A single batch has no such host
  intervention between its dispatches.
- CPU `Buffer::read()` and `Buffer::write()` remain prohibited while any
  uncollected submission references that buffer, regardless of the submitted
  range or access mode.

Use a single batch when the operations form a fixed GPU-side sequence. Use
separate submissions when the host must observe completion, access results, or
decide what to submit next.

## Lifetime

The `socl::DispatchToken` returned by `submitAsync()` owns the submitted command
buffer and fence and retains all captured descriptor snapshots, pipelines, and
buffers. It also owns one CPU-access claim for each distinct buffer in the
submission. `socl::DispatchToken::wait()`, token destruction, or replacement of
a valid token waits for GPU completion before those retained resources and
claims are released. If several uncollected submissions use one buffer, all of
their claims must be released before CPU access is allowed.
