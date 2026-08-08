# Development Prompt 014

## User request

버퍼를 descriptor set에 바인딩할 때 각 버퍼의 접근 의도를 `Read`, `Write`, `ReadWrite` 중 하나로 함께 지정하고, `Context::dispatch()`가 호출된 시점의 실제 바인딩 상태를 기준으로 메모리 보호와 동기화를 수행한다.

## What to implement

- 버퍼 binding에 shader 접근 모드(`Read`, `Write`, `ReadWrite`)를 저장한다.
- 각 dispatch가 사용하는 pipeline, descriptor set, buffer 및 접근 모드를 dispatch 시점에 snapshot한다.
- 동일 command batch 안에서 같은 버퍼를 연속 dispatch가 사용할 경우 접근 충돌에 맞는 Vulkan buffer memory barrier를 자동 기록한다.
- 기록된 리소스와 제출된 리소스가 GPU 완료 전에 파괴되거나 안전하지 않게 변경되지 않도록 보호한다.
- 비동기 제출 중 CPU의 Buffer read/write 및 DescriptorSet 변경이 GPU 접근과 충돌하지 않도록 상태를 추적한다.

## How to implement

- 공개 enum `BufferAccess { Read, Write, ReadWrite }`를 추가하고 `DescriptorSet::bindBuffer(binding, buffer, access)` 형태로 받는다.
- DescriptorSetState의 각 binding에 BufferState와 BufferAccess를 함께 저장한다.
- `dispatch()`에서 현재 descriptor bindings를 buffer identity 기준으로 병합하여 해당 dispatch의 접근 집합을 만든다. 같은 버퍼가 여러 binding에 연결되면 가장 강한 접근 모드로 병합한다.
- command recording 동안 buffer별 마지막 접근을 추적한다. 이전 접근과 현재 접근 중 하나라도 write이면 현재 dispatch 직전에 compute-shader buffer barrier를 삽입하고, read-read에는 삽입하지 않는다.
- dispatch 시 사용한 descriptor set과 pipeline을 recording batch가 강하게 참조하고, submit 시 DispatchToken으로 이동하여 fence 완료까지 유지한다.
- 제출 시 buffer별 in-flight read/write 상태를 등록하고, token wait 완료 시 해제한다. CPU read는 GPU write와, CPU write는 모든 GPU access와 충돌하도록 검사한다.
- 한 번 dispatch에 사용된 descriptor set은 해당 기록/제출이 끝날 때까지 update 또는 rebind하지 못하도록 보호하여 이미 기록된 명령의 descriptor 의미가 바뀌지 않게 한다.

## Synchronization rules

| Previous access | Current access | Barrier |
| --- | --- | --- |
| Read | Read | No |
| Read | Write / ReadWrite | Yes |
| Write / ReadWrite | Read / Write / ReadWrite | Yes |

## Scope for this turn

요구사항과 구현 범위를 정리한다. `prompts` 이외의 파일은 사용자 허락을 받은 후 변경하며, 빌드나 실행은 하지 않는다.
