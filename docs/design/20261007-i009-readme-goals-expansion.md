# #9 설계 : README 목표 절 확장 / #9 design : expanding README's goals section

이슈: [#9](https://github.com/reexec/rex86/issues/9) | 지시서: [20261007-i009](../work-orders/20261007-i009-readme-goals-expansion.md) | 로그: [20261007-i009](../work-logs/20261007-i009-readme-goals-expansion.md)

## 요구사항 / Requirement

사용자가 #3의 다섯 목표에 더해 무엇이 필요한지 물었고, 논의에서 다음이 합의되었다: 새 목표 네 개(견고성/격리, 자원 예산, 반응성/계층화, 관측성)를 더하고, 메모리·주소 관리는 성격에 따라 나눠 반영한다 — 주소 변환 의미와 SMC 감지는 기존 목표 1·2의 명시 항목·완료 기준으로, 코드 캐시 운영은 자원 예산 목표로, 게스트 메모리 할당과 게스트 페이징은 비목표로.

*The user asked what belongs beyond #3's five goals, and the discussion settled on four new goals (robustness/sandboxing, resource budget, responsiveness/tiering, observability), with memory and address management split by nature: address-translation semantics and SMC detection are promoted into goals 1–2, code-cache operation belongs to the resource budget, and guest-memory allocation and guest paging are stated as non-goals.*

## 설계 결정 / Design decisions

* **목표 1 보강**: 주소 생성과 세그먼테이션 의미(base/limit 검사와 #GP, 16비트 세그먼트의 64KiB wrap, 주소 크기 prefix wrap, 스택 wrap, 폴트 종류)를 명시 항목으로 적는다. census에 잡히지 않는 횡단 관심사이고 SingleStepTests(real mode)가 32비트 보호 모드 세그먼테이션을 덮지 못하므로 전용 테스트가 측정 수단이다.
* **목표 2 보강**: SMC 감지("`kTranslated` 페이지에 쓰기 후 이전 번역이 절대 실행되지 않는다")를 완료 기준에 더하고, SingleStepTests(#7)를 독립 측정 수단으로 명시한다.
* **목표 6 견고성/격리**: 임의 바이트열과 임의 상태에서 호스트 무크래시, `GuestMemory` 경계 밖 접근 불가. 측정: 디코더·인터프리터·번역기 fuzzing, ASan/UBSan CI 작업. 웹 데모 공개(목표 5) 전에 성립해야 한다.
* **목표 7 자원 예산**: wasm 모듈 크기 상한, 런타임 메모리 상한, 코드 캐시 예산과 축출의 정확성. 수치는 4단계(웹 호스트) 측정에서 확정하고 그때까지 CI 크기 리포트로 추세만 추적한다.
* **목표 8 반응성/계층화**: 첫 프레임까지의 시간, 번역으로 인한 프레임 스톨 없음(p99 프레임 시간), 인터프리터 선행 + 백엔드 비동기 워밍업 구조의 검증. 수치는 성능 목표(3)와 같은 시점에 확정.
* **목표 9 관측성**: trace 기록·재생, 단일 스텝, 상태 덤프. 완료 기준은 "모든 불일치는 trace로 재현 가능"이다. 목표 2의 측정이 여기 의존한다.
* **비목표 명시**: 게스트 메모리 할당·배치(소비자의 일), 게스트 페이징(사용자 모드 코어), 멀티스레드 게스트(두 소비자 모두 단일 게스트 스레드)를 목표 절 끝에 적고 헌장을 링크한다.
* **채택하지 않은 것**: 소비자 통합(단독 측정 원칙과 충돌, 달성도 표 2A/2B로 유지), API 안정성(AGENTS.md release tag 규칙과 중복), 에너지 효율.
* 달성도 표: 견고성 하네스 행(목표 6)을 더하고, 기존 행들의 관련 목표 번호를 갱신한다(trace가 있는 1단계에 9, 벤치마크 행에 7·8).
* 코드 변경 없음. 공개 계약과 소비자 영향 없음.

*Goal 1 gains the address-generation and segmentation semantics (base/limit checks and #GP, 16-bit 64KiB wrap, address-size-prefix wrap, stack wrap, fault kinds) as explicit items measured by dedicated tests, since the census cannot see this cross-cutting concern and the real-mode SingleStepTests cannot cover 32-bit protected-mode segmentation. Goal 2 gains SMC detection ("after a write to a `kTranslated` page, a stale translation never executes") in its completion bar and names SingleStepTests (#7) as an independent instrument. New goals: 6 robustness/sandboxing (no host crash on arbitrary bytes and state, no access beyond `GuestMemory`; measured by fuzzing and an ASan/UBSan CI job; must hold before the web demo ships), 7 resource budget (wasm module size, runtime memory, code-cache budget and eviction correctness; numbers fixed at phase 4, size reports tracked in CI until then), 8 responsiveness/tiering (time to first frame, no translation-induced frame stalls at p99, the interpreter-first tiering verified; numbers fixed with goal 3's), and 9 observability (trace record/replay, single-step, state dumps; done when every divergence reproduces from a trace, which goal 2's measurement depends on). Non-goals stated at the section's end with a charter link: guest-memory allocation/placement (the consumers'), guest paging (a user-mode core), multithreaded guests (both consumers run one guest thread). Not adopted: consumer integration (conflicts with standalone measurability; stays as phases 2A/2B), API stability (duplicates the release-tag rules), energy efficiency. The attainment table gains the robustness-harness row and updated goal numbers. No code change, no contract or consumer impact.*
