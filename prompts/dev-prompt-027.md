# Development Request Summary

## User request

vucol 자체 테스트는 하나 skip되는 것 외에 문제가 없는데, vuBLAS에서 vucol을 쓸 때 문제가 생긴다. 코드는 그냥 `vucol::Context` 생성이다.

## What to implement

- vuBLAS에서 `vucol::Context ctx;` 생성 시점에 발생하는 실패의 원인을 진단한다.
- 유력 가설: vucol 쪽 "하나의 skip"이 GPU 테스트(`BufferRangeTest.PreserveOrderAcrossDispatchesAndSubmissions`)이며, 이 테스트는 `Context::enumerateGpus()`가 비어 있거나 예외를 던지면 `GTEST_SKIP()`한다. 즉 vucol 테스트는 GPU 경로를 한 번도 실행하지 않고 통과한 것이고, skip 가드가 없는 vuBLAS 테스트(`tests/NaiveTest.cpp`에서 각 테스트가 `vucol::Context ctx;`를 직접 생성)는 같은 환경 조건에서 하드 실패(예외/abort)한다.
- 가설을 확정하려면 개발자가 직접 실행한 결과물이 필요하다 (에이전트 환경에서 컴파일/실행 금지): vucol 테스트의 skip 사유 메시지, vuBLAS 실패 출력(예외 문구 포함), 실행 환경(macOS 여부, Vulkan SDK/MoltenVK/ICD 상태).

## How to implement

- 읽기 전용으로 확인한 사실 (실행 없이):
  - `/opt/vucol/include/vucol`와 작업본 `include/vucol`은 동일하고 (`diff -rq` 차이 없음), `/opt/vucol/lib/libvucol.0.0.10.dylib`와 작업본 `build` 산출물은 크기와 타임스탬프가 일치한다. 즉 설치본-작업본 불일치(stale install) 가설은 제외된다.
  - vuBLAS 테스트에는 `GTEST_SKIP`/`enumerateGpus` 가드가 전혀 없다. vucol의 `BufferRangeTest.cpp:25-32`에만 skip 가드가 있다.
  - `Context::enumerateGpus()` (`src/Context.cpp:950`)와 `Context` 생성자 (`src/Context.cpp:340`)는 같은 `createVkbInstance()` + `makePhysicalDeviceSelector()` 경로를 쓰므로, 한쪽이 GPU를 못 보면 다른 쪽도 실패한다.
- 개발자에게 요청할 실행 결과물:
  - vucol 테스트를 verbose로 실행했을 때의 skip 사유 한 줄 (`Vulkan compute device enumeration failed: ...` 또는 `No Vulkan compute device is available.`).
  - vuBLAS에서 실패하는 테스트명과 전체 실패 출력 (gtest의 예외 메시지 포함).
  - `vulkaninfo --summary` 또는 MoltenVK/Vulkan SDK 설치 여부, 두 테스트를 같은 터미널·같은 환경변수로 실행했는지 여부.
## Confirmed root cause (2026-10-01, 추가 확인)

- 개발자가 보고한 skip 사유: `Vulkan compute device enumeration failed: vk-bootstrap instance creation failed: vulkan_unavailable`.
- `vulkan_unavailable`은 vk-bootstrap `SystemInfo::get_system_info()` (`build/_deps/vk-bootstrap-src/src/VkBootstrap.cpp:494-498`)에서 `init_vulkan_funcs(nullptr)` 실패 시 반환된다. Apple 플랫폼에서 이는 `dlopen("libvulkan.dylib")` → `libvulkan.1.dylib` → `libMoltenVK.dylib`이 모두 실패했다는 뜻 (같은 파일 129-134행) — 즉 실행 머신에 Vulkan 로더/MoltenVK 자체가 없다. 인스턴스 생성·portability·디바이스 선택 이전 단계의 실패이므로 vucol 코드 버그가 아니다.
- 결과 정리: vucol GPU 테스트는 설계대로 skip, skip 가드가 없는 vuBLAS 테스트는 `vucol::Context` 생성자에서 그대로 throw. 코드 수정 불필요.

- 조치 방향 (개발자 작업, 에이전트 환경 실행 금지):
  - macOS에 LunarG Vulkan SDK 설치 (MoltenVK + 로더 포함), SDK 환경 설정 후 `vulkaninfo --summary`에서 MoltenVK 디바이스가 보이는지 확인.
  - 같은 터미널·같은 환경변수로 두 테스트 스위트 재실행.
  - 선택 사항: GPU 없는 CI에서도 vuBLAS 테스트가 skip되도록 vucol `BufferRangeTest.cpp:25-32`와 같은 `enumerateGpus()` 가드를 vuBLAS 쪽에 추가 (vuBLAS repo 변경이므로 별도 승인 필요). `prompts/` 외 파일 변경 전에는 사용자 승인을 받는다.

## Follow-up: brew로 설치했는데도 실패 (2026-10-01, 추가 확인)

- 머신 상태: Apple Silicon (`brew --prefix` = `/opt/homebrew`), `molten-vk`·`vulkan-loader` 모두 설치됨. `/opt/homebrew/lib/libvulkan.dylib`, `libMoltenVK.dylib` 존재. ICD 매니페스트는 `/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json`에 존재 (driver 경로는 매니페스트 기준 상대경로 `../../../lib/libMoltenVK.dylib`).
- 그럼에도 실패하는 이유: vk-bootstrap은 로더를 파일명만으로 `dlopen("libvulkan.dylib")` 한다 (`VkBootstrap.cpp:131-134`). macOS에서 파일명-only `dlopen`은 `DYLD_LIBRARY_PATH`와 기본 fallback(`$HOME/lib:/usr/local/lib:/lib:/usr/lib`)만 뒤지는데, 현재 쉘은 `DYLD_LIBRARY_PATH`가 비어 있고 `/opt/homebrew/lib`은 fallback에 없다. 빌드가 성공한 것은 CMake가 절대경로로 링크했기 때문이며, 런타임 `dlopen`과는 무관하다.
- 다음 관문: 로더가 떠도 드라이버(ICD) 탐색이 남는다. brew의 ICD 경로는 로더 기본 탐색 경로가 아니므로 `VK_ICD_FILENAMES` 지정이 함께 필요하다.
- 처방 (개발자 작업):
  - `export DYLD_LIBRARY_PATH=/opt/homebrew/lib`
  - `export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json`
  - 같은 쉘에서 `vulkaninfo --summary`로 MoltenVK 디바이스 확인 후 두 테스트 스위트 재실행.
