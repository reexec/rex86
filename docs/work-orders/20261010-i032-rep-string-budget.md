# #32 작업 지시 : REP 문자열 반복을 실행 예산에 세기 / #32 work order : counting REP string iterations against the budget

이슈: [#32](https://github.com/reexec/rex86/issues/32) | 설계: [20261010-i032](../design/20261010-i032-rep-string-budget.md) | 로그: [20261010-i032](../work-logs/20261010-i032-rep-string-budget.md)

## 작업 항목 / Tasks

1. 기준선: 수정 전 `main`의 Release 벤치마크를 같은 기계에서 잰다(번갈아 재기 위해 별도 빌드를 둔다).
2. 인터프리터:
   * `Ctx`에 허용 단계 수, `attention`, 실행한 단계 수를 둔다.
   * `ExecuteStrings`는 반복마다 단계를 세고, 허용을 다 쓰거나 `attention`이 서면 `ExecStatus::kPartial`로 돌아간다(설계 결정 1, 2).
   * `StepStatus::kPartial`과 `StepResult::steps`를 더한다.
   * 반복 도중 폴트와 거절된 INS/OUTS는 끝난 반복을 센다.
   * `interp::Step`은 허용 무제한이 기본값이다.
3. `RunBlock`과 `Cpu`:
   * 남은 단계를 허용으로 넘긴다.
   * `kPartial`이면 진행 중인 명령의 선형 주소를 기억하고, 같은 주소에서는 게이트 검사를 건너뛴다(결정 3).
   * `Cpu::InstructionInProgress()`를 더한다.
4. 공개 계약의 이름을 바꾼다(결정 4): `Event::steps`, `Run(step_budget)`, 헤더 주석.
5. 하네스(결정 5):
   * trace 재생의 `kSingleStep`은 명령이 끝날 때까지 `Step()`을 되풀이한다.
   * 벤치마크, 견고성 하네스, probe의 이름을 바꾸고, `string` 워크로드의 주석을 고친다.
   * x87/SIMD trace 스텁에 REP가 없는지 확인한다.
6. 단위 테스트 `rep_budget_test.cpp`: 설계의 검증 절에 적은 경우 전부를 캐시가 없을 때와 있을 때 둘 다 확인한다. 기존 테스트의 기대값을 갱신한다.
7. 검증:
   * 단위 테스트, 정수(i386)·x87·SIMD fuzz, trace 묶음 전부, 견고성 하네스(ASan 포함), SST(로컬에 묶음이 있으면).
   * 로컬 빌드 전부: x86-64 Debug, Release, Clang, ASan/UBSan(예열 기본값과 0), i386, wasm32.
   * 벤치마크를 전후로 번갈아 잰다.
8. 문서:
   * ARCHITECTURE(공개 계약, 실행 루프), README(목표 8, 현재 수치), 분석 `interpreter-performance.md`(단위 변경과 전후).
   * kb `trap-flag-single-step.md`에서 REP 중단 상태의 SDM 근거를 보강한다.
   * 작업 로그.

*1 baseline: measure `main`'s Release benchmark before the change on the same machine (a separate build, for interleaving); 2 interpreter: `Ctx` carries the step allowance, `attention` and the steps run; `ExecuteStrings` counts a step per iteration and returns `ExecStatus::kPartial` once the allowance is used up or `attention` is raised (design decisions 1 and 2); `StepStatus::kPartial` and `StepResult::steps` are added; a mid-string fault or declined INS/OUTS counts the iterations completed; `interp::Step` defaults to an unlimited allowance; 3 `RunBlock` and `Cpu`: the remaining steps passed as the allowance; on `kPartial` the instruction's linear address is remembered and the gate check skipped at that address (decision 3); `Cpu::InstructionInProgress()`; 4 the public contract's renames (decision 4): `Event::steps`, `Run(step_budget)`, header comments; 5 harnesses (decision 5): trace replay's `kSingleStep` repeats `Step()` until the instruction finishes; the benchmark, robustness harness and probe renamed, the `string` workload's comment corrected; checking that the x87/SIMD trace stubs have no REP; 6 unit tests in `rep_budget_test.cpp` for every case in the design's verification section, with and without the cache, and updated expectations in existing tests; 7 verification: unit tests, the integer (i386), x87 and SIMD fuzz, every trace corpus, the robustness harness (ASan included), SST where the suite is present locally; every local build (x86-64 Debug, Release, Clang, ASan/UBSan at both warm-ups, i386, wasm32); the benchmark before and after, interleaved; 8 documents: ARCHITECTURE (the public contract, the run loop), README (goal 8, current figures), the analysis `interpreter-performance.md` (the unit change, before and after), the kb `trap-flag-single-step.md` with the SDM's basis for the suspended REP state, and the work log.*

## 완료 조건 / Completion criteria

* `Run(budget)`이 REP 문자열 도중에도 budget 단계에서 돌아오고, `RequestStop`과 대기 인터럽트를 반복 경계에서 처리한다.
* 모든 대조 fuzz, trace 묶음, 견고성 하네스, SST, 단위 테스트가 통과한다. 아키텍처 결과는 바뀌지 않는다.
* REP가 아닌 워크로드의 벤치마크가 잡음 범위 안이다.
* 다섯 호스트 CI와 libFuzzer 작업이 녹색이다.

*`Run(budget)` returns at budget steps even inside a REP string and handles `RequestStop` and pending interrupts at iteration boundaries; every comparison fuzz, trace corpus, the robustness harness, SST and the unit tests pass, with architectural results unchanged; the benchmark's workloads without REP stay within the noise; CI is green on the five hosts and the libFuzzer job.*
