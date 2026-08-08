# Development Prompt 015

## User request

1. `dispatch()` 시점의 buffer binding 상태를 snapshot하여 GPU 작업 완료까지 메모리를 보호한다.
2. `DescriptorSet::bindBuffer()`가 `Read`, `Write`, `ReadWrite` 접근 정보를 추가 인자로 받고 기본값은 `ReadWrite`로 한다.
3. 여러 연산을 기록한 뒤 `submitAsync()` 한 번으로 CPU의 중간 개입 없이 GPU가 끝까지 실행할 수 있게 한다.
4. CPU에서 설정하는 buffer binding이 GPU 명령으로 전달되는 방식과, 사용자 측에서 descriptor set을 연산마다 별도로 만들지 않고도 서로 다른 buffer 연산을 한 batch에 넣을 수 있는지 설명한다.

## What to implement

- 공개 `BufferAccess` enum과 기본 접근값이 `ReadWrite`인 `bindBuffer()` API를 추가한다.
- logical DescriptorSet의 현재 binding 상태를 `dispatch()` 때 immutable Vulkan descriptor set으로 snapshot한다.
- 한 logical DescriptorSet을 dispatch 사이에 다시 bind/update해도 앞선 dispatch의 descriptor가 변경되지 않게 한다.
- dispatch별 buffer 접근 정보를 이용해 같은 command buffer 안의 충돌하는 접근 사이에 GPU buffer memory barrier를 자동 삽입한다.
- 기록 및 제출된 command batch가 사용하는 descriptor snapshot, pipeline, buffer를 fence 완료까지 강하게 참조한다.
- GPU가 읽거나 쓰는 동안 충돌하는 CPU buffer 접근을 거부한다.

## How to implement

- `DescriptorSetState`의 각 binding에 `BufferState`와 `BufferAccess`를 함께 저장한다.
- `Context::bind()`는 logical DescriptorSet을 선택하고, 실제 Vulkan descriptor bind는 `dispatch()`로 옮긴다.
- `dispatch()`는 현재 binding 상태로 내부 descriptor pool/set을 만들고 update한 다음 이를 command buffer에 bind한다.
- 동일 buffer가 한 dispatch의 여러 binding에 나타나면 접근 모드를 병합한다.
- 이전 dispatch와 현재 dispatch 중 하나라도 write 접근이면 compute-to-compute buffer barrier를 현재 dispatch 직전에 기록한다. read-to-read에는 barrier를 넣지 않는다.
- batch resource holder가 기록 시점부터 buffer 접근 claim과 descriptor snapshot을 소유한다. `submitAsync()`는 holder를 `DispatchToken`으로 이동하고 `wait()` 완료 시 해제한다.
- `Buffer::read()`는 GPU write claim과, `Buffer::write()`는 모든 GPU claim과 충돌할 경우 예외를 발생시킨다.

## Expected usage

```cpp
context.begin();

set.bindBuffer(0, bufferA, socl::BufferAccess::ReadWrite);
context.bind(set);
context.dispatch(...); // A binding snapshot

set.bindBuffer(0, bufferB, socl::BufferAccess::ReadWrite);
context.bind(set);
context.dispatch(...); // B binding snapshot

auto token = context.submitAsync();
```

After submission, the GPU executes both immutable descriptor snapshots and any inserted barriers without CPU intervention.

## Constraints

- Do not compile or execute the project in the agent environment.
