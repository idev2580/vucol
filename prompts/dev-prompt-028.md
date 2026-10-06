# Development Request Summary

## User request

Vulkan 로더 문제는 해결됐는데, subgroup 32를 쓰려 하자 `Required subgroup sizes are not supported for compute shaders.` 에러가 난다. 사용자는 이것이 진짜 subgroup 문제인지 의심한다.

## What to implement

- 에러 문구의 정확한 출처와 조건을 코드에서 특정하고, 드라이버 리포트 문제인지 vucol 쿼리 버그인지 판별한다.
- 판별이 되면 그에 맞는 조치를 정한다 (요구값 제거/조건부화 vs. vucol 폴백 vs. 현행 유지).

## How to implement

- 읽기 전용으로 확인한 사실 (실행 없이):
  - 문구는 `src/Context.cpp:492-494` 딱 한 곳에서만 throw된다. 조건은 드라이버가 보고한 `requiredSizeStages`에 compute 비트가 없는 경우이다. 값의 출처는 `VkPhysicalDeviceSubgroupSizeControlProperties::requiredSubgroupSizeStages`를 그대로 복사한 것 (`src/Context.cpp:229-230`). vucol이 가공하지 않으므로, 쿼리 체인(`makeSubgroupSupportInfo`, 196-237행)이 정상이라면 문구는 드라이버 리포트를 정확히 전달한 것이다.
  - 32를 요구하는 쪽은 vuBLAS이다: `src/ops/Gemm.cpp:51`, `src/ops/MatMul.cpp:47`, `src/ops/GemmOutPlace.cpp:48`에 `.requiredSubgroupSize = 32` 하드코딩.
  - 일반 subgroup 연산 지원과 required-size 지원은 별개 Vulkan 기능이다. 이 에러는 후자(강제 사이즈 지정 knob)에만 해당하므로 "subgroup 전체 고장"은 아니다.
  - vucol은 드라이버 원본 값을 그대로 노출한다: `Context::subgroupSupportInfo()` (`include/vucol/Context.hpp:526`) — `defaultSize`, `minSize`/`maxSize`, `sizeControlSupported`/`Enabled`, `requiredSizeStages`를 개발자가 직접 출력해 볼 수 있다.
- 판별 probe (개발자 실행, 에이전트 환경 실행 금지):
  - `ctx.subgroupSupportInfo()` 각 필드 출력 + `vulkaninfo`의 subgroup size control 항목을 대조한다.
  - `requiredSizeStages`에 compute가 없으면 드라이버(MoltenVK) 제한 확정, 있으면 vucol 버그이므로 보고받아 재조사한다.
- probe 결과 (2026-10-01, Apple M4 / MoltenVK, `/tmp/subgroup_probe.cpp`로 확인): `requiredSizeStages = 0x0` (어떤 스테이지도 required size 미지원), `sizeControlSupported/Enabled = 1`, `defaultSize = 32`, `min/max = 4/32`. 즉 드라이버 제한 확정, vucol 버그 아님. `src/Context.cpp:492` 조건은 이 값에서 필연적으로 throw한다. 덤으로 `defaultSize = 32`라서 32 고정은 이 GPU에서 no-op이다.
- 조치 방향 (probe 결과 후, `prompts/` 외 변경은 승인 필요):
  - Apple Silicon은 기본 subgroup 크기가 32인 경우가 많아 요구 자체가 no-op일 수 있다. `requiredSubgroupSize`를 생략하거나 플랫폼 조건부로 바꾸는 쪽이 유력 (vuBLAS repo 변경).
  - vucol이 throw 대신 폴백하도록 바꾸는案은 동작 계약 변경이므로 별도 결정이 필요하다.
