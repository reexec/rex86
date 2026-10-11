# #50 작업 지시 : 진행 중인 REP 문자열의 재개와 wasm 캠페인 작업 / #50 work order : resuming a REP string in progress, and the wasm campaign job

이슈: [#50](https://github.com/reexec/rex86/issues/50) | 설계: [20261011-i050](../design/20261011-i050-rep-resume-and-wasm-campaign.md) | 로그: [20261011-i050](../work-logs/20261011-i050-rep-resume-and-wasm-campaign.md)

## 작업 항목 / Tasks

1. `interp`: `StepBudget`에 `kPartial` 때 디코드를 복사할 칸을 더한다. `exec_strings`가 `kPartial`로 돌아갈 때 복사한다. `BlockLimits`에 `resume`(저장한 디코드)을 더한다. `RunBlock`은 `resume`이 있으면 첫 단계를 루프 밖에서 그 디코드로 실행한다(설계 결정 1).
2. `Cpu`: 저장 칸(처음 필요할 때 만듦), 저장할 때의 CS 모드, 재개 조건(같은 선형 EIP, 같은 모드, `in_progress_`), 단계를 실행하면 무효. `cpu.h`의 `InstructionInProgress` 주석에 재개 규칙을 적는다.
3. 테스트:
   - `rep_budget_test`: 자기 바이트를 덮어쓰는 REP 셋(STOSB, DF=1 STOSD, MOVSB)의 분할 동등성. 같은 프로그램의 번역(평가기, 문턱 0)과 인터프리터 비교. EIP나 CS 모드를 바꾼 재개는 다시 가져옴.
   - `robust_test`: `seed=2200826386` 회귀.
4. wasm 캠페인 작업(설계 결정 2).
5. 검증: Windows x64 Release 단위 테스트와 견고성(이 seed와 10만 건 이상), wasm32 Node ctest, 벤치마크 `string`·`mixed` 전후. 다섯 호스트 CI.
6. 문서: #32 설계에 재개 규칙을 덧붙이는 참조, ARCHITECTURE, README 경고 문구(번역 백엔드가 있음, #45 이후), kb(필요하면), 작업 로그, 이슈 #50 코멘트.

*1 `interp`: a slot in `StepBudget` receiving the decode on `kPartial`, copied by `exec_strings` when it returns `kPartial`; a `resume` field (the saved decode) in `BlockLimits`, `RunBlock` running the first step outside its loop on that decode when given (design decision 1). 2 `Cpu`: the slot (made when first needed), CS's mode at the save, the resume condition (same linear EIP, same mode, `in_progress_`), invalidation on any step; the resume rule in `cpu.h`'s `InstructionInProgress` comment. 3 Tests: `rep_budget_test` gains the split equivalence of three self-overwriting REPs (STOSB, STOSD with DF=1, MOVSB), the same programs under translation (evaluator, threshold 0) against the interpreter, and a re-fetch when EIP or CS's mode changed before resuming; `robust_test` pins `seed=2200826386`. 4 The wasm campaign job (design decision 2). 5 Verification: Windows x64 Release unit tests and robustness (this seed and 100,000 cases or more), wasm32 Node ctest, the benchmark's `string` and `mixed` before and after, CI on five hosts. 6 Documents: a pointer from #32's design to the resume rule, ARCHITECTURE, the README warning (translation backends exist since #45), the kb if needed, the work log, a comment on #50.*

## 완료 조건 / Completion criteria

* `rex86_robust --case 2200826386`이 위반 없이 끝난다.
* 새 단위 테스트가 바꾸기 전 코드에서 실패하고, 바꾼 뒤 통과한다.
* 다섯 호스트 CI, libFuzzer, `browsers`가 녹색이다. 벤치마크의 `string`·`mixed`가 잡음 범위 안이다.
* 이 판의 tag 캠페인에서 robust-release와 wasm-translate가 녹색이다(tag push 뒤 확인, 로그에 날짜별 절).

*`rex86_robust --case 2200826386` ends without a violation; the new unit tests fail on the code before the change and pass after; CI on five hosts, libFuzzer and `browsers` is green, the benchmark's `string` and `mixed` within the noise; this version's tag campaign has robust-release and wasm-translate green (checked after the tag push, a dated section in the log).*
