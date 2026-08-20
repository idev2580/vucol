# 개발 요청 요약

## 사용자 요청

최근 추가된 다중 dispatch 배치 기능에서 각 `dispatch()`가 descriptor set 등의 상태를 snapshot으로 보존할 때, dispatch 사이에 선택된 `ShaderPipeline`이 변경되는 경우에도 각 dispatch가 해당 pipeline을 올바르게 캡처하고 유지하는지 현재 구현을 확인한다.

## 확인할 내용 (What to implement)

- 코드 변경 없이 현재 동작을 분석한다.
- 각 `dispatch()` 시점에 Vulkan pipeline bind 명령이 기록되는지 확인한다.
- 선택된 pipeline의 수명과 descriptor snapshot의 pipeline 연관성이 `submitAsync()` 및 GPU 완료 시점까지 유지되는지 확인한다.
- pipeline 변경이 지원되지 않거나 주의할 제약이 있다면 명확히 설명한다.

## 확인 방법 (How to implement)

- `Context::use()`, `Context::bind()`, `Context::dispatch()`, `Context::submitAsync()`의 상태 및 command recording 흐름을 추적한다.
- `DispatchResources`와 descriptor snapshot이 보유하는 `ShaderPipelineState` 참조를 확인한다.
- 관련 공개 API 문서와 구현이 일치하는지 대조해 답변한다.
- 프로젝트 지침에 따라 컴파일이나 실행은 하지 않는다.
