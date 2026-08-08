# Development Prompt 013

## User request

socl의 여러 GPU 명령 일괄 기록·제출 기능을 soclBLAS의 계산 계획 전체를 한 번에 제출하는 용도로 사용할 때, GPU 실행 중 관련 메모리가 해제되거나 변경되지 않도록 보호하는 로직을 추가할 수 있는지 검토한다.

## What to implement

- 현재 command recording 및 sync/async submission 과정에서 Buffer, DescriptorSet, ShaderPipeline의 수명이 GPU 작업 완료 시점까지 보장되는지 분석한다.
- soclBLAS가 하나의 계산 계획을 기록하고 한 번에 제출할 때 필요한 메모리 수명 보호와 명령 간 메모리 가시성/동기화 요구사항을 구분한다.
- SOCL의 단순성을 유지하면서 적용할 수 있는 최소 설계를 제안한다.

## How to implement

- 기록 중 사용된 BufferState, DescriptorSetState, ShaderPipelineState를 command batch 단위로 수집한다.
- submitAsync 시 해당 리소스 소유권을 DispatchToken으로 이동하여 fence 완료 전까지 파괴되지 않게 하고, wait 또는 token 정리 시 fence 완료 후 해제한다.
- 동일 command buffer 내 연속 dispatch 사이의 RAW/WAR/WAW hazard에는 Vulkan buffer memory barrier를 기록할 수 있는 API 또는 보수적인 자동 barrier 정책을 검토한다.
- CPU read/write와 진행 중인 GPU 접근의 충돌은 wait 또는 명시적 dependency를 통해 방지하며, 단순 객체 수명 보호와 데이터 race 방지를 별개의 기능으로 다룬다.

## Scope for this turn

코드를 수정하거나 빌드·실행하지 않고, 현재 구현을 근거로 가능 여부와 권장 설계를 설명한다.
