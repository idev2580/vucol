# Development Request Summary

## User request

`createShaderPipeline`에서 `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo`를 스테이지 pNext에 거는 if문도 Apple일 때만 빼달라.

## What to implement

- `src/Context.cpp`의 파이프라인 생성부에서 `createInfo.requiredSubgroupSize` 체이닝을 `#if defined(__APPLE__)`로 감싸 macOS에서는 요구값 구조체를 달지 않는다. 주석 포함.
- 앞선 dev-prompt-029의 검사 skip과 합치면, macOS에서 `requiredSubgroupSize`는 power-of-two·min/max 검사만 받고 드라이버에는 전달되지 않으며 기본 subgroup 크기(M4에서 32)에 의존하게 된다.

## How to implement

- 선언문까지 `#else` 안에 넣어 Apple 빌드에서 unused-variable 경고가 나지 않게 한다 (선언만 밖에 두고 사용만 가리면 경고 가능).
- 주석에 이유 명시: MoltenVK의 `requiredSubgroupSizeStages`는 비어 있어서 체이닝 자체가 invalid usage이며 `createComputePipeline`이 거부할 수 있고, Metal SIMD 폭은 고정이라 기본값에 의존해도 32 요청과 결과가 같다.
- 검증은 개발자 작업 (에이전트 환경 컴파일/실행 금지): 리빌드 → `/opt/vucol` 재설치 → vuBLAS 실행으로 파이프라인 생성 성공 확인.
