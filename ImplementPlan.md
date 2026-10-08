## Background — Vulkan Device Group을 하나의 Logical Device로 묶을 때 헷갈리는 점

Vulkan 1.1 코어인 Device Group(`VK_MAX_DEVICE_GROUP_SIZE` = 32)은 여러 `VkPhysicalDevice`를
하나의 `VkDevice`로 묶는 기능이다. VUCOL은 이 단일 논리 디바이스 모델을 그대로 따른다.

1. **실행 큐는 논리 디바이스당 1개다.** 물리 GPU별로 큐를 만들지 않는다. 어느 GPU가 실행할지는
   커맨드 기록·제출 시 `deviceMask`로 정한다: `VkDeviceGroupCommandBufferBeginInfo::deviceMask`
   → 필요시 `vkCmdSetDeviceMask` → 제출 시 `VkDeviceGroupSubmitInfo`의 마스크.
   VUCOL은 `begin` 1회–`submitAsync` 1회 사이에 `dispatch(x, y, z, deviceIndex)`마다 마스크를
   전환하므로, 한 배치 안에 여러 GPU 명령을 기록할 수 있다. 기록 순서가 실행 순서다.
2. **ShaderPipeline이 공유되는 이유:** `VkPipeline`, `VkDescriptorSetLayout`은 논리 디바이스
   소속 객체라 물리 디바이스별 사본이 없다. 셰이더 바이너리·레이아웃이 같으므로 1개로 충분하고,
   어느 디바이스 실행에도 바인딩할 수 있다. 단 `requiredSubgroupSize` 검증은 그룹 내 모든
   디바이스의 교집합(가장 엄격한 조건)으로 수행한다.
3. **DescriptorSet이 공유되는 이유:** `VkDescriptorSet`도 논리 디바이스 객체이며, 내용은
   `(VkBuffer 핸들, 오프셋, 범위)` 참조일 뿐이다. `VkBuffer` 핸들 역시 논리 디바이스 소속이라
   디바이스별 복제가 필요 없다. 실행 가능 여부는 (a) 커맨드의 `deviceMask`와
   (b) 바인딩된 메모리가 그 디바이스에서 접근 가능한지(소유 메모리 또는 피어 접근)로 판정되며,
   이 검사는 `dispatch` 시점에 한다. 그래서 `createDescriptorSet`·`bindBuffer`·`update`에는
   인덱스가 없다.
4. **버퍼는 단일 디바이스에 할당된다.** `VkMemoryAllocateFlagsInfo{ flags = DEVICE_MASK_BIT,
   deviceMask = 1u << owner }`로 정확히 하나의 디바이스 메모리에 할당한다. 타 디바이스 접근은
   피어 메모리(`vkGetDeviceGroupPeerMemoryFeatures`의 `GENERIC_SRC`/`DST`)로만 성립한다.
5. **VMA를 쓰지 않는다.** 사용 중인 VMA 소스에서 `deviceMask` 언급이 0건이므로 device-mask
   할당을 지원하지 않는다. 버퍼·스테이징 할당은 전부 raw `vkAllocateMemory`로 직접 구현한다.
   (VMA FetchContent/link는 건드리지 않고, 소스에서만 사용을 제거한다.)
6. **같은 그룹에 속한 GPU끼리만 묶을 수 있다.** `vkEnumeratePhysicalDeviceGroups`로 요청
   인덱스들이 동일 그룹인지 검증하고, 아니면 생성자에서 예외를 던진다. 서로 다른 그룹의
   GPU(iGPU+dGPU 등 드라이버가 묶어주지 않은 조합)는 v1에서 묶을 수 없다.

## Goal

단일 논리 디바이스(Device Group) 기반 다중 GPU 지원을 `Context`에 추가한다. 버퍼 생성·
디스패치·디바이스별 조회는 `deviceIndex`(기본값 0)를 받으며, 메서드마다 시그니처는 정확히
1개다(호환용 오버로딩을 만들지 않는다). `begin` 1회–`submitAsync` 1회 사이에 여러 GPU의
명령을 기록할 수 있다. 이번 대화에서는 `prompts/`에 기록하지 않는다.

## Success Criteria

- `ContextCreateInfo{ physicalDeviceIndices, requiredDeviceExtensions }`로 같은 그룹의
  GPU들을 묶어 하나의 `Context`를 생성할 수 있다.
- `createBuffer`, `dispatch`, 디바이스별 조회는 모두 `uint32_t deviceIndex = 0` 인자를
  가지며, 생략 시 무조건 0번 GPU가 선택된다. `begin`/`beginTimed`/`submitAsync`에는
  인덱스가 없다. 각 메서드는 단일 시그니처이며, 기존 호출부 수정이 필요할 수 있음을
  허용한다(오버로딩으로 우회하지 않는다).
- `Buffer`는 `ownerDeviceIndex` 1개와 피어 접근 플래그(`ALLOW_PEER_READ`,
  `ALLOW_PEER_WRITE`)를 가진다. 소유 디바이스가 아닌 곳에서의 접근은 플래그가 허용할
  때만 성공하고, 보호(배리어·접근 추적·예외)가 자동으로 동작한다.
- 하드웨어가 피어 접근을 지원하지 않는 조합에 피어 플래그를 지정하면 버퍼 생성 시점에
  예외가 발생한다.
- 기존 테스트(`BasicTest`, `BufferRangeTest`)가 단일 GPU 환경에서 그대로 통과한다.

## 메모리 보호 규칙 (구체 로직)

접근 기록의 단위는 `(버퍼, 오프셋 구간, 디바이스, 접근모드)`다. 기존 `(버퍼, 구간, 접근모드)`
추적에 실행 디바이스를 추가한 형태이며, 정규화·오버레이 알고리즘은 그대로 재사용한다.

### 의존성 추적 규칙

- **R1. 기록 단위:** 각 `dispatch`는 실행 디바이스 `d`와 함께 스냅샷의 모든 바인딩을
  `(buffer, offset, size, access, device = d)` 집합으로 만든다. 서로 다른 디바이스의 사용은
  정규화 시 절대 병합하지 않는다.
- **R2. 허용 검사(기록 시점):** 실행 디바이스 == 소유 디바이스이면 선언된 `BufferAccess`대로
  허용한다. 실행 디바이스 != 소유 디바이스이면 P1의 피어 커버 검사를 통과해야 하며, 실패하면
  즉시 `throw`한다(바인딩 번호·소유 디바이스·실행 디바이스·요구 접근을 메시지에 포함).
- **R3. 배리어 판정:** 현재 사용과 겹치는 이전 접근이 있고, 둘 중 하나라도 쓰기를 포함하면
  배리어를 삽입한다. 둘 다 읽기(read-after-read)이면 배리어를 생략한다. 디바이스가
  같든 다르든(피어 허용된 경우) 이 규칙은 동일하다.
- **R4. 배리어 기록 위치:** `dispatch`는 `vkCmdSetDeviceMask(1u << d)` 전환 후에 배리어를
  기록한다. 즉 배리어는 실행 디바이스의 마스크 아래에서 실행되며, 단일 큐 제출 순서와 함께
  가시성을 보장한다.
- **R5. 시드(seed):** 배치 시작 시 각 버퍼의 `lastQueueAccesses`(디바이스 포함)를 배치 추적기에
  심는다. 이전에 제출된 접근과 겹치면 R3에 따라 배리어가 생긴다.
- **R6. 커밋:** `submitAsync` 성공 시 배치의 최종 접근 맵을 각 버퍼의 `lastQueueAccesses`에
  덮어쓴다(디바이스 정보 포함). 실패 시 롤백되어 이전 상태가 유지된다.
- **R7. CPU 접근 보호:** `Buffer::write`/`read`는 미수거 GPU 제출(`gpuUseClaims != 0`)이
  있으면 거부한다. 기록 중(미제출) 버퍼는 CPU 접근이 허용된다. 기존 동작 유지.
- **R8. 바인딩-실행 일치:** `dispatch`는 스냅샷 순서 == 파이프라인 바인딩 선언 순서임을 이용해
  위반 메시지에 바인딩 번호를 포함한다.

### 피어 보호 규칙

- **P1. 커버 판정:** 요구 접근 `Read`→`PeerRead` 비트, `Write`→`PeerWrite` 비트,
  `ReadWrite`→두 비트 모두 필요. 하나라도 모자라면 R2에서 `throw`한다.
- **P2. 생성 시점 검증:** `peerAccess != None`인 버퍼는 생성 시점에 소유 디바이스의 메모리 힙에
  대해 그룹 내 모든 타 디바이스와의 `vkGetDeviceGroupPeerMemoryFeatures`를 조회한다.
  읽기 허용이면 `GENERIC_SRC`, 쓰기 허용이면 `GENERIC_DST`가 전 쌍에서 필요하고, 하나라도
  없으면 `throw`한다.
- **P3. 자동 마이그레이션 없음:** 피어 접근은 원격 메모리의 직접 접근이지 복사가 아니다.
  디바이스 간 자동 복사·마이그레이션은 v1 비목표다.
- **P4. 스테이징 경로:** `write`/`read`의 스테이징 복사는 소유 디바이스 마스크로 기록·제출한다.

### 플래그 정의 (`XXX` = `PEER`, Vulkan 피어 메모리 용어)

```cpp
enum class BufferPeerAccess : std::uint32_t {
    None = 0,
    PeerRead = 1u << 0,   // ALLOW_PEER_READ: 타 디바이스에서 읽기 허용
    PeerWrite = 1u << 1,  // ALLOW_PEER_WRITE: 타 디바이스에서 쓰기 허용
};
// PeerReadWrite = PeerRead | PeerWrite (읽기·수정 모두 허용)
```

`createBuffer(bytes, type, deviceIndex = 0, peerAccess = None)` 단일 시그니처로만 제공한다.

## Approach (설계 결정 요약)

1. **그룹 생성:** `ContextCreateInfo::physicalDeviceIndex` → `std::vector<uint32_t>
   physicalDeviceIndices = {0}`. 생성자는 2개 유지. `vk-bootstrap`은 물리 디바이스 선택까지만
   쓰고, 논리 디바이스는 `VkDeviceGroupDeviceCreateInfo`를 `pNext`로 체인한 raw
   `vkCreateDevice`로 직접 만든다(동기화2·서브그룹 피처 체인 이식 포함). `detail::ContextState`
   의 물리·속성·`GpuInfo`·capability를 디바이스별 벡터로 바꾼다. 큐 패밀리는 전 디바이스 동일
   인덱스의 컴퓨트 패밀리를 요구하고, 다르면 예외(v1 제한).
2. **단일 배치 다중 디바이스:** `begin`/`submitAsync` 인덱스 없음. `dispatch`마다 마스크 전환.
   `use`/`bind`/`push` 공유 상태라 인덱스 없음.
3. **조회는 전부 디바이스별:** 11개 조회(`bufferOffsetAlignment`, `usingIntegratedGpu`,
   `supportsGpuTiming`, `gpuTimingSupportInfo`, `gpuInfo`, `supportedDeviceExtensions`,
   `supportsDeviceExtension`, `supportsCooperativeMatrix`, `cooperativeMatrixSupportInfo`,
   `subgroupSupportInfo`, `printGpuInfo`)에 `uint32_t deviceIndex = 0` 추가(단일 시그니처).
   `enumerateGpus()` 변경 없음.
4. **`bindBuffer` 검증은 그룹-엄격 기준:** 디스크립터셋이 공유되므로 바인드 시점에는 실행
   디바이스를 모르며, 오프셋 정렬은 전 디바이스 중 최대값, 최대 범위는 최소값으로 검증한다.
5. **타임스탬프:** 기존 단일 쿼리풀 경로 유지. 다중 디바이스 배치의 측정값은 근사치임을 문서화한다.
6. **OS 분기 추가 금지.** Windows/macOS/Linux 공통 경로.

## Steps

1. **공개 헤더 (`include/vucol/Buffer.hpp`, `include/vucol/Context.hpp`):** `BufferPeerAccess`
   + 비트 연산 + `Buffer::ownerDeviceIndex()`/`peerAccess()` 접근자. `ContextCreateInfo`
   교체, `createBuffer`/`dispatch`/11개 조회 시그니처 변경(단일 시그니처, 기본값 0).
2. **내부 상태 (`src/InternalState.hpp`):** `ContextState` 벡터화 + `usedMask`·`currentDevice`
   추적. `BufferState`를 VMA에서 수동 메모리(`vk::DeviceMemory`, 매핑 포인터, coherent 여부,
   `ownerDeviceIndex`, `peerAccess`)로 교체. `detail` 헬퍼 선언(피어 커버 검사, 메모리 타입
   선택, 단일-디바이스 할당).
3. **그룹 디바이스 생성 (`src/Context.cpp`):** 그룹 검증 + raw `vkCreateDevice`
   (`VkDeviceGroupDeviceCreateInfo` + synchronization2 + subgroup-size-control 체인).
   디바이스별 큐 패밀리·타임스탬프·메모리 속성·capability 수집.
4. **수동 메모리 할당 (`src/Buffer.cpp` + 헬퍼):** 메모리 타입 선택(HostVisible 요청은
   HOST_VISIBLE 중 DEVICE_LOCAL 우선, DeviceLocal 경로는 DEVICE_LOCAL 비노출 우선 후
   폴백), `DEVICE_MASK_BIT` 단일 비트 할당·바인드, host-visible이면 영속 매핑, P2 피어 검증.
   VMA 사용 제거(빌드 의존성은 유지).
5. **기록·제출 (`begin`/`dispatch`/`submitAsync`, 스테이징 복사):** `begin`은 전체 마스크로
   명시 시작. `dispatch`는 R2 검사 → 마스크 전환 → R3 배리어 → 바인드 → 디스패치 →
   `currentDevice`·`usedMask` 갱신. `submitAsync`는 `VkDeviceGroupSubmitInfo`(사용 마스크)로
   1회 제출. 스테이징 복사는 소유 마스크로 제출.
6. **보호 로직 (`src/DispatchResources.hpp/.cpp`):** `DispatchBufferUse`·`BufferAccessRange`에
   디바이스 필드, 정규화 키에 디바이스 포함, R1–R8/P1 구현.
7. **`bindBuffer` 그룹-엄격 검증 (`src/DescriptorSet.cpp`).**
8. **호출부 수정 (`examples/elementwise_add.cpp`, `tests/BasicTest.cpp`):** 오버로딩 없이
   신 시그니처로 직접 수정.
9. **신규 테스트 (`tests/MultiDeviceContextTest.cpp`, glob 자동 포함):** (a) 인덱스 생략 시
   0번 선택, (b) 범위 밖 인덱스 예외, (c) 피어 없는 크로스 디바이스 사용 예외,
   (d) `ALLOW_PEER_READ` 읽기 성공, (e) `ALLOW_PEER_WRITE` 쓰기 성공,
   (f) 미지원 피어 조합 생성 예외, (g) 한 배치 다중 디바이스 순서 실행.
   멀티 GPU 없으면 (c)~(g) 스킵. `BufferPeerAccess` 비트 단위 순수 테스트 포함.

## Validation Plan

- 컴파일·실행은 에이전트 환경에서 하지 않고 개발자가 수행한다(`AGENTS.md` 준수).
- `cmake --build build` 성공 후 `ctest` 전체 통과(기존 테스트 회귀 없음).
- Steps 9항의 (a)~(g) 확인. 단일 GPU 머신에서는 (c)~(g)가 스킵되어야 한다.
- 수정된 예제 실행으로 0번 GPU 경로 동작 확인.
- 멀티 GPU 머신에서 2-GPU 그룹 생성 + 디바이스별 할당 + 피어 읽기/쓰기 + 위반 예외를
  수동 시나리오로 확인. `beginTimed` 다중 디바이스 측정값은 참고용으로만 본다.

## Risks / Open Questions

- **VMA 미지원 확정:** VMA 소스 내 `deviceMask` 0건 확인. 수동 할당으로 가며, 메모리 타입
  선택·flush/invalidate를 직접 관리해야 한다.
- **같은 그룹 제약:** 드라이버가 묶어주지 않은 GPU 조합은 생성자 예외. 분할 그룹(논리 디바이스
  여러 개)은 v1 비목표.
- **큐 패밀리 동일 인덱스 요구:** 그룹 내 전 디바이스가 같은 인덱스에 컴퓨트를 노출해야 한다(v1).
- **피어 미지원 하드웨어:** 해당 환경에서는 크로스 디바이스 접근 불가(단일 디바이스만 사용).
- **비목표:** 자동 마이그레이션·복사, `MultiDeviceContext.hpp` 신설계, OS별 분기 추가.
