# 버퍼 서브레인지와 메모리 보호 로직 정리

## 사용자 요청 요약

같은 버퍼 오브젝트를 `offset`과 `size`로 여러 구간으로 나누고, 각 구간을 셰이더에서 서로 다른 버퍼처럼 바인딩해 사용할 수 있도록 한다. 이에 맞춰 메모리 보호 로직을 CPU-GPU 보호와 GPU-GPU 보호로 구분해 정리한다.

## 구현할 내용

- 버퍼 사용/바인딩 단위에 `offset`과 `size`를 추가한다.
- 동일한 버퍼의 서로 다른 서브레인지를 독립적인 셰이더 버퍼 바인딩으로 사용할 수 있게 한다.
- 한 submit 안의 여러 dispatch 사이에서 RAW, WAR, WAW 등 GPU-GPU 메모리 위험을 판별하고 필요한 동기화를 수행한다.
- 기존 Snapshot 구조를 유지하면서 서브레인지 단위의 접근 상태와 충돌을 표현할 방안을 설계한다.
- CPU가 버퍼를 읽거나 쓰는 동안 GPU submit과 충돌하지 않도록 하는 정책을 정리한다.
- CPU-GPU 보호를 코드로 강제할지, 사용 규약으로만 문서화할지 현재 구조를 근거로 평가한다.

## 구현/설계 방향

- 현재 버퍼, descriptor binding, dispatch/submit, Snapshot 및 host mapping 경로를 먼저 조사한다.
- 버퍼 전체가 아니라 `[offset, offset + size)` 범위의 겹침 여부를 기준으로 GPU-GPU hazard를 판정한다.
- 기존 Snapshot의 역할과 수명은 유지하되, 각 접근 기록에 버퍼 식별자와 범위 및 접근 종류를 포함시키는 최소 변경안을 우선 검토한다.
- Vulkan descriptor의 offset/range 제약, 정렬, 범위 초과 및 overflow 검증 지점을 명확히 한다.
- CPU-GPU 보호는 안전성, API 단순성, 동기화 비용, 비동기 submit과 mapped memory의 실제 추적 가능성을 비교해 코드 강제와 convention 방식의 장단점을 평가한다.
- 분석 후 구체적인 수정 파일과 API/동작 변경안을 사용자에게 제시하고, `prompts` 외 파일을 수정하기 전에 허락을 받는다.

## 대화 중 확정된 방향

- `dispatch()`에서는 Snapshot을 만들고 해당 batch가 사용하는 버퍼, 범위, 접근 종류만 수집한다. 이 시점에는 전역 GPU claim을 활성화하지 않는다.
- `submitAsync()`가 실제 queue submit을 수행하기 직전에 batch의 모든 버퍼 claim을 사전 검사한 뒤 한꺼번에 활성화한다.
- CPU-GPU 보호는 첫 구현에서 범위별로 세분화하지 않는다. submit이 버퍼의 일부만 사용하더라도 해당 버퍼 오브젝트 전체에 대한 CPU `read()`와 `write()`를 막는다.
- claim은 GPU 완료가 `DispatchToken::wait()` 등으로 확인되고 batch 자원이 해제될 때까지 유지한다.
- 기존 동작은 가능한 한 유지한다. 특히 서로 다른 live batch의 read/read 공유는 허용하고, writer가 포함된 기존 batch 간 충돌 규칙은 submit 시점의 claim 검사로 옮긴다.
- 한 submit 내부 dispatch 사이의 GPU-GPU 보호는 Snapshot에 저장된 offset/size 범위의 실제 겹침을 기준으로 RAW, WAR, WAW barrier를 생성한다.
- 같은 Context는 하나의 Vulkan queue와 하나의 recording만 소유하지만, 앞선 비동기 submission의 token이 완료되기 전에 다음 batch를 기록하고 같은 queue에 submit할 수 있다는 점을 설계에 반영한다.
- 같은 queue의 submission들은 CPU wait 없이 연속 제출할 수 있어야 한다. 같은 버퍼가 여러 submission에서 사용되더라도 submission 자체를 거부하지 않고, 이전 submission과 다음 submission 사이의 RAW, WAR, WAW를 범위별 Vulkan buffer memory barrier로 자동 연결한다.
- queue에 성공적으로 submit된 범위별 마지막 GPU 접근 상태를 Context/버퍼와 함께 유지하고, 다음 recording의 dispatch가 해당 상태 및 현재 batch의 상태를 기준으로 barrier를 기록한다. 전역 마지막 dispatch 하나가 아니라 버퍼의 각 범위별 마지막 접근을 보존한다.
- CPU-GPU claim은 GPU-GPU 충돌 거부 용도가 아니라 CPU `read()`/`write()` 차단 용도로만 사용한다. 여러 미완료 submission이 같은 버퍼를 사용하면 submission별 claim을 유지하고 각각 완료가 회수될 때 해제한다.
- 자동 보호는 정확한 `BufferAccess` 선언, descriptor 범위 준수, SOCL이 관리하는 단일 Context queue를 전제로 한다.

## 필수 검증 시나리오

- 실제 GPU에서 실행되는 통합 테스트를 추가한다.
- 하나의 물리 버퍼를 offset/size를 사용해 최소 3개 이상의 구간으로 나누고 서로 다른 shader buffer binding처럼 사용한다.
- dispatch 순서를 바꾸면 최종 값이 달라지는 연산을 구성하여 barrier와 실행 순서가 결과에 실제로 반영되는지 확인한다.
- 동일 batch 내부의 여러 dispatch뿐 아니라, 같은 버퍼를 다시 사용하는 여러 `submitAsync()`에 걸친 실행도 포함한다.
- 앞선 submission을 CPU에서 기다리지 않고 다음 submission을 queue에 넣은 뒤, 마지막 token 완료 후 결과를 확인한다.
- 저장소 규칙에 따라 에이전트 환경에서는 컴파일하거나 실행하지 않으며, 테스트 코드와 개발자용 실행 방법 및 기대 결과를 제공한다.
