# Tutorial: AXPY and Batched Dispatch {#tutorial_axpy}

This tutorial implements the BLAS level-one AXPY operation:

```text
y[i] = alpha * x[i] + y[i]
```

It first records one AXPY dispatch, then shows how to record several AXPY
operations into one command batch. The example keeps the GLSL source in the C++
program and uses SOCL's shaderc-backed `socl::compileGlslToSpirv()` function, so
no external shader file or compilation command is required.

## Complete single-AXPY example

```cpp
#include <socl/Context.hpp>
#include <socl/ShaderCompiler.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uint32_t workgroupSize = 64;

struct AxpyConstants {
    float alpha;
    std::uint32_t count;
};

constexpr const char* axpyShader = R"glsl(
#version 450

layout(local_size_x = 64) in;

layout(set = 0, binding = 0) readonly buffer XBuffer {
    float values[];
} x;

layout(set = 0, binding = 1) buffer YBuffer {
    float values[];
} y;

layout(push_constant) uniform AxpyConstants {
    float alpha;
    uint count;
} constants;

void main() {
    const uint index = gl_GlobalInvocationID.x;
    if (index < constants.count) {
        y.values[index] = constants.alpha * x.values[index] + y.values[index];
    }
}
)glsl";
}

int main() {
    // Compile the embedded GLSL compute shader to SPIR-V in this process.
    const auto spirv = socl::compileGlslToSpirv(axpyShader, "axpy.comp");

    socl::Context context;
    auto pipeline = context.createShaderPipeline({
        .spirv = spirv,
        .bindings = {
            {0, socl::DescriptorType::StorageBuffer},
            {1, socl::DescriptorType::StorageBuffer},
        },
        .pushConstantSize = sizeof(AxpyConstants),
    });

    std::vector<float> x = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f, 40.0f};
    const float alpha = 2.0f;
    const auto count = static_cast<std::uint32_t>(x.size());
    const std::size_t bytes = x.size() * sizeof(float);

    // HostVisible keeps this first example simple. BufferType::Auto is usually a
    // better default for application code and still supports write()/read().
    auto xBuffer = context.createBuffer(bytes, socl::BufferType::HostVisible);
    auto yBuffer = context.createBuffer(bytes, socl::BufferType::HostVisible);
    xBuffer.write(x.data(), bytes);
    yBuffer.write(y.data(), bytes);

    auto descriptorSet = context.createDescriptorSet(pipeline);
    descriptorSet.bindBuffer(0, xBuffer, socl::BufferAccess::Read);
    descriptorSet.bindBuffer(1, yBuffer, socl::BufferAccess::ReadWrite);

    const AxpyConstants constants{alpha, count};

    context.begin();
    context.use(pipeline);
    context.bind(descriptorSet);
    context.push(constants);
    context.dispatch((count + workgroupSize - 1) / workgroupSize);
    auto token = context.submitAsync();

    // read() must not race the GPU write, so wait before reading yBuffer.
    token.wait();
    yBuffer.read(y.data(), bytes);

    for (float value : y) {
        std::cout << value << ' ';
    }
    std::cout << '\n'; // 12 24 36 48
}
```

Binding zero is declared `Read` because the shader only reads `x`. Binding one
is `ReadWrite` because AXPY reads the old value of `y` and writes the result back
to the same buffer. These access declarations must match the shader; SOCL uses
them for conflict checks and inter-dispatch barriers.

`begin()` starts one command batch. `use()`, `bind()`, `push()`, and `dispatch()`
record commands into that batch. `submitAsync()` closes and submits it, and the
returned `socl::DispatchToken` retains all resources until completion. See
@ref dispatch_snapshots for the complete recording and lifetime model.

## Measuring GPU execution time

`socl::Context` automatically queries and enables Vulkan 1.3
`synchronization2` during device creation when the selected GPU supports it.
The application does not need to request a timing extension or enable a feature
manually. Check `supportsGpuTiming()` before starting a timed batch because the
selected compute queue must also expose timestamp bits:

```cpp
if (!context.supportsGpuTiming()) {
    throw std::runtime_error("GPU timing is unavailable");
}

context.beginTimed();
context.use(pipeline);
context.bind(descriptorSet);
context.push(constants);
context.dispatch((count + workgroupSize - 1) / workgroupSize);

auto token = context.submitAsync();
const socl::GpuDuration gpuDuration = token.waitAndGetGpuDuration();
std::cout << "GPU batch: "
          << std::chrono::duration<double, std::micro>(gpuDuration).count()
          << " us\n";
```

`beginTimed()` records a top-of-pipe timestamp before the batch commands, and
`submitAsync()` records a bottom-of-pipe timestamp before ending the command
buffer. The result therefore covers pipeline and descriptor binds, automatic
barriers, and every dispatch in that batch. It does not include CPU command
recording, CPU submission overhead, or time spent waiting before this command
buffer starts executing. Buffer upload or download submissions performed
outside the batch are also excluded.

Each timed asynchronous batch owns a separate timestamp query pool through its
`DispatchToken`, so independent timed tokens may remain in flight together.
Calling ordinary `wait()` on a timed token also collects and caches the timing
result; a later `waitAndGetGpuDuration()` returns that cached value.

## Recording several AXPY operations in one submission

An execution plan can reuse the same pipeline and logical descriptor set for
many AXPY operations. Assume each job already owns initialized `x` and `y`
buffers:

```cpp
struct AxpyJob {
    socl::Buffer x;
    socl::Buffer y;
    float alpha;
    std::uint32_t count;
};

std::vector<AxpyJob> jobs = createAxpyJobs(context);
auto descriptorSet = context.createDescriptorSet(pipeline);

context.begin();
context.use(pipeline);

for (const AxpyJob& job : jobs) {
    descriptorSet.bindBuffer(0, job.x, socl::BufferAccess::Read);
    descriptorSet.bindBuffer(1, job.y, socl::BufferAccess::ReadWrite);
    context.bind(descriptorSet);

    const AxpyConstants constants{job.alpha, job.count};
    context.push(constants);
    context.dispatch((job.count + workgroupSize - 1) / workgroupSize);
}

auto token = context.submitAsync(); // one queue submission for every AXPY above
```

Every `dispatch()` creates a new native descriptor snapshot. Rebinding
`descriptorSet` for the next job does not change any earlier dispatch. Each
push-constant value and workgroup count is also recorded in command order, so
each AXPY uses its own buffers, `alpha`, and `count`.

The loop records several commands into one command buffer; it does not call
`submitAsync()` once per job. SOCL submits the recorded sequence once and the GPU
processes it in command order. This batching model does not promise that the
AXPY operations execute concurrently. It removes intermediate CPU submissions
and lets the Vulkan implementation schedule independent work when legal.

If jobs use different buffers, no buffer dependency is required between them.
If jobs share a buffer and at least one use writes it, SOCL records a
compute-to-compute barrier based on the declared `BufferAccess` modes. For
example, two jobs may update the same `y` sequentially in one batch; the second
dispatch observes the first dispatch's write when both uses are correctly
declared `ReadWrite`.

Wait for the returned token before reading any `y` buffer written by the batch.
To execute the same logical plan again, call `begin()`, replay the job loop, and
call `submitAsync()` again. A completed command buffer cannot be resubmitted by
calling `submitAsync()` a second time without a new recording.
