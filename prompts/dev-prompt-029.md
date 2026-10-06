# Development Request Summary

## User request

`createShaderPipeline`의 required-subgroup-size 스테이지 검사를 macOS에서는 건너뛰도록 해달라. 해당 `runtime_error`를 macOS에서만 skip하고 주석을 남길 것.

## What to implement

- `src/Context.cpp`의 `createShaderPipeline` 내 `requiredSizeStages & eCompute` 검사를 `#if defined(__APPLE__)`로 감싸 macOS에서는 skip한다. power-of-two·min/max 범위 검사는 그대로 둔다.
- MoltenVK 상황(probe: Apple M4, `requiredSizeStages = 0x0`, `defaultSize = 32`)을 설명하는 주석을 남긴다.

## How to implement

- 검사 블록 하나만 프리프로세서 분기한다. 다른 로직·시그니처 변경 없음.
- 주의 (코드에도 주석으로 명시): 검사를 건너뛰면 `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo`가 그대로 체인되어 `createComputePipeline`으로 넘어가므로, 실패 지점이 드라이버 파이프라인 생성 단계로 이동할 수 있다. 스펙상 requiredSizeStages가 비어 있는 스테이지에 required size를 거는 것은 invalid usage라서, MoltenVK가 파이프라인 생성을 거부하면 다음 에러는 `createComputePipeline failed.`가 된다.
- 검증은 개발자 작업 (에이전트 환경 컴파일/실행 금지): macOS에서 리빌드 후 `/opt/vucol`에 재설치하고 vuBLAS 실행. 파이프라인 생성까지 실패하면 대안은 Apple에서 요구값 자체를 빼고(default 32에 의존) 생성하는 폴백이며, 이는 별도 결정이 필요하다.
