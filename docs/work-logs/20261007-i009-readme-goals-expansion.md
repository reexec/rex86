# #9 작업 로그 : README 목표 절 확장 / #9 work log : expanding README's goals section

이슈: [#9](https://github.com/reexec/rex86/issues/9) | 설계: [20261007-i009](../design/20261007-i009-readme-goals-expansion.md) | 지시서: [20261007-i009](../work-orders/20261007-i009-readme-goals-expansion.md)

## 2026-10-07

- **출발점**: 사용자가 #3의 다섯 목표 외에 더 필요한 것을 물었고, 제안(견고성/격리, 자원 예산, 반응성/계층화, 관측성)과 메모리·주소 관리 논의(주소 변환 의미와 SMC는 기존 목표 승격, 할당·페이징은 비목표)를 승인했다.
- **구현**: README 목표 절을 다섯에서 아홉으로 확장했다.
  - 목표 1에 주소 생성·세그먼테이션 의미(base/limit와 #GP, 16비트 wrap, 주소 크기 prefix wrap, 스택 wrap, 폴트 종류)와 경계값 전용 테스트를 명시. census가 못 보는 횡단 관심사라는 근거를 적었다.
  - 목표 2에 SingleStepTests(#7)를 독립 측정 수단으로, SMC("`kTranslated` 쓰기 후 이전 번역 실행 금지")를 완료 기준으로 추가.
  - 새 목표 6 견고성/격리(fuzzing, ASan/UBSan, 웹 데모 전 성립), 7 자원 예산(wasm 크기, 메모리 상한, 코드 캐시 예산·축출; 수치는 4단계 확정), 8 반응성/계층화(p99 프레임 시간, time-to-first-frame; 구조 기준은 즉시 적용), 9 관측성(모든 불일치는 trace로 재현 가능).
  - 비목표 소절: 게스트 메모리 할당·배치, 게스트 페이징, 멀티스레드 게스트, 게임 로직·로더·HLE·그래픽. 헌장 링크.
  - 달성도 표: 1단계에 목표 9, 3단계에 8, 벤치마크 행에 7·8을 더하고 견고성 하네스 행(목표 6)과 4단계의 자원 예산 확정을 추가.
- **채택하지 않은 것**: 소비자 통합(단독 측정 원칙과 충돌), API 안정성(release tag 규칙과 중복), 에너지 효율. 설계에 근거를 남겼다.
- **검증**: 코드 변경 없음. README의 새 링크(SingleStepTests, 헌장)와 세 작업 문서의 상호 링크가 유효함을 확인했다.
- **남은 것**: 없음. 머지는 사용자 요청 시 절차대로.

*Starting point: the user asked what belongs beyond #3's five goals and approved the proposal — four new goals plus the memory/address split (address-translation semantics and SMC promoted into existing goals, allocation and paging as non-goals). Implementation: README grows from five to nine goals — goal 1 gains address-generation and segmentation semantics with dedicated boundary tests (a cross-cutting concern the census cannot see), goal 2 gains SingleStepTests (#7) as an independent instrument and the SMC bar ("no stale translation runs after a `kTranslated` write"), and the new goals are 6 robustness/sandboxing (fuzzing, ASan/UBSan, holding before the web demo), 7 resource budget (wasm size, memory ceiling, code-cache budget and eviction; caps fixed at phase 4), 8 responsiveness/tiering (p99 frame time, time-to-first-frame; the structural bar applies now) and 9 observability (every divergence reproduces from a trace); a non-goals subsection (guest-memory allocation/placement, guest paging, multithreaded guests, game logic/loaders/HLE/graphics) links the charter, and the attainment table gains the new goal numbers, the robustness-harness row and phase 4's cap-fixing. Not adopted, with reasons in the design: consumer integration, API stability, energy efficiency. Verification: no code change; the new README links and the three task documents' cross-links were checked. Nothing remains; the merge follows the procedure when the user asks.*
