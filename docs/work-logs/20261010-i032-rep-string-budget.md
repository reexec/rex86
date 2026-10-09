# #32 작업 로그 : REP 문자열 반복을 실행 예산에 세기 / #32 work log : counting REP string iterations against the budget

이슈: [#32](https://github.com/reexec/rex86/issues/32) | 설계: [20261010-i032](../design/20261010-i032-rep-string-budget.md) | 지시서: [20261010-i032](../work-orders/20261010-i032-rep-string-budget.md)

## 2026-10-10 (진행 중 / in progress)

- **맥락 확인**:
  - 두 소비자(rePIU, re2DJ) 저장소에는 `rex86`을 언급하는 파일이 하나도 없다. 2A·2B단계가 시작되지 않았으므로 v0.0.18의 tag 올림 작업은 만들지 않았다.
  - 코어는 TF 단일 스텝 트랩을 구현하지 않는다. SST 러너는 `interp::Step`을 직접 부른다. trace 재생의 단일 스텝 모드는 `Cpu::Step`을 쓰고, 정수 fuzz의 호스트 쪽은 REP 반복 트랩마다 이어 실행해 명령 하나를 끝까지 잰다.
  - 벤치마크 `mixed`의 바퀴(203,307)는 다른 다섯 워크로드의 합이므로 `string` 커널을 포함한다.
- **설계 승인**(사용자): 반복 하나 = 한 단계(결정 1), 공개 계약의 이름을 지금 바꾼다(결정 4). 설계 결정 3에 기억 주소가 하나인 한계를 더했다.
- **구현**:
  - `Event::steps`, `Cpu::Run(step_budget)`, `Cpu::Step` = 한 단계, `Cpu::InstructionInProgress()`.
  - `StepStatus::kPartial`, `ExecStatus::kPartial`.
  - `RunBlock`이 `StepBudget`(허용, `attention`, 문자열의 단계 보고)을 빌려준다. `ExecuteStrings`는 반복을 세고 반복 사이에서 멈춘다.
  - `Cpu`는 진행 중인 REP의 선형 주소에서 게이트 검사를 건너뛴다.
  - trace 재생의 `kSingleStep`은 `InstructionInProgress()`가 거짓이 될 때까지 `Step()`을 되풀이한다.
  - 도구의 출력 키(`retired=`, `lap_instructions=`, `mips=`)는 가이드와 분석이 참조하므로 이름을 유지하고, 단계를 센다고 가이드에 적었다.
- **단위 테스트** `rep_budget_test.cpp`, 캐시가 없을 때와 있을 때 둘 다:
  - REP 프로그램 11개를 예산 1, 2, 7로 나눈 실행과 한 번에 실행한 결과가 같다. MOVS, STOS, LODS, CMPS, SCAS, INS, OUTS, DF=1, 16비트 주소의 감쌈, REPE/REPNE 조기 종료, count 0, REP 둘을 다룬다.
  - 반복 사이의 예산 소진, `Step` 한 번이 반복 하나인지, 반복 중 `RequestStop`.
  - 포트 콜백에서 올린 인터럽트가 반복 사이에서 전달되고(복귀 EIP = 문자열 명령) IRET 뒤에 이어지는지.
  - 진행 중인 REP의 게이트를 건너뛰는 것과, EIP가 옮겨진 뒤 다시 검사하는 것.
  - count 0의 한 단계, 반복 도중 폴트와 거절된 INS의 단계 수.
  - 처음 실패 세 번은 모두 테스트 코드 탓이었다: null CS 선택자, 실행 불가능한 CS 디스크립터, 핸들러가 포트 번호가 든 EDX를 덮어씀.
  - 기존 테스트 두 곳의 기대값을 새 단위로 고쳤다(REP MOVSB 5 → 8, 거절된 REP OUTSB 3 → 4). 단위 checks 1,850, 실패 0.
- **빌드와 ctest**(`-Werror`, 첫 구현 기준): 11개 구성이 모두 통과했다.
  - x86-64 Debug, Release, Release 예열 0, Clang Debug, ASan/UBSan 기본 예열과 예열 0: 각 7/7.
  - i386 Debug, Debug 예열 0, Release, Release 예열 0: 각 8/8.
  - wasm32(emsdk 3.1.74): 5/5, `rex86_probe` `result=ok`.
- **fuzz**(첫 구현 기준): 사용자 요청으로 캠페인 도중 중단했다.
  - 정수 i386 기본 예열 100만 건(시드 3201): 불일치 0, `vendor_deviations=14`.
  - 정수 i386 예열 0 100만 건(시드 3202): 불일치 0, `vendor_deviations=16`.
  - x87 100만 건(시드 3203): 불일치 0.
  - SIMD x86-64 100만 건(시드 3204)과 i386 30만 건(시드 3205): 불일치 0.
  - trace 묶음 18,396건: x86-64, i386 모두 불일치 0.
  - 견고성 하네스의 긴 실행은 중단으로 결과가 없다. ctest의 300건 smoke만 모든 구성에서 통과했다.
  - **미확정**: 정수 fuzz의 `vendor_deviations` 14와 16이 이 기계(AMD Ryzen 5 5600X)에서 수정 전에도 나오는 값인지 비교하지 않았다.
- **벤치마크**(AMD Ryzen 5 5600X, 네이티브 Linux, x86-64 GCC Release, 수정 전 `main`과 번갈아 세 번씩, 중앙값):

  | 워크로드 | 수정 전 | 첫 구현 | `StepBudget` 구현 |
  |---|---|---|---|
  | alu | 37.66 | 34.75(−7.7%) | 36.35(−5.9%) |
  | memory | 40.43 | 38.90(−3.8%) | 40.19(−2.0%) |
  | call | 42.04 | 39.32(−6.5%) | 38.94(−7.6%) |
  | string | 7.74 | 78.56(단위 변경) | 미측정 |

  - 첫 구현은 명령마다 `Ctx`의 필드 셋, 허용과 `attention` 인자, `StepResult::steps`를 썼다.
  - `StepBudget` 구현은 `RunBlock`의 스택에 예산 하나를 두고 `Ctx`에 포인터 하나만 넘긴다. 단계 보고는 문자열 명령만 한다(소멸자로 모든 출구에서 보고).
  - 그래도 alu와 call의 회귀가 남았다. 완료 조건(REP가 아닌 워크로드는 잡음 범위 안)을 아직 지키지 못한다.
  - `string` 워크로드의 바퀴 단계 수는 53,258에서 569,354가 됐다(반복을 셈).
  - x87과 mixed 줄은 측정 스크립트의 파싱이 놓쳤다. 다시 재야 한다.

*Context: neither consumer repository has any file mentioning `rex86` (phases 2A and 2B not started), so no tag-bump task was made for v0.0.18; the core implements no TF single-step trap, the SST runner calls `interp::Step` itself, trace replay's single-step mode uses `Cpu::Step`, and the integer fuzz's host side runs through every REP iteration trap to measure one whole instruction; the benchmark's `mixed` lap (203,307) is the sum of the other five, the `string` kernel included. Design approved by the user: one iteration = one step (decision 1), and the public contract's names change now (decision 4); decision 3 gained the one-remembered-address limit. Implemented: `Event::steps`, `Cpu::Run(step_budget)`, `Cpu::Step` as one step, `Cpu::InstructionInProgress()`, `StepStatus::kPartial` and `ExecStatus::kPartial`, a `StepBudget` (allowance, `attention`, the string's step report) lent by `RunBlock`, `ExecuteStrings` counting iterations and stopping between them, `Cpu` skipping the gate check at a REP in progress, trace replay's `kSingleStep` repeating `Step()` until `InstructionInProgress()` is false; the tools' output keys (`retired=`, `lap_instructions=`, `mips=`) keep their names, which the guides and analyses reference, and the guide says they count steps. Unit tests in `rep_budget_test.cpp`, with and without the cache: eleven REP programs (MOVS, STOS, LODS, CMPS, SCAS, INS, OUTS, DF=1, 16-bit address wrap, REPE/REPNE ending early, count 0, two REPs) equal run in budgets of 1, 2 and 7 and at once; the budget running out between iterations, one `Step` being one iteration, `RequestStop` mid-string; an interrupt raised in a port callback delivered between iterations (return EIP = the string instruction) and continued after IRET; skipping the gate at a REP in progress and checking it again once EIP moved; one step for count 0; the steps of a mid-string fault and a declined INS. The first three failures were all the test's: a null CS selector, a non-executable CS descriptor, and a handler overwriting EDX, which held the port. Two existing expectations changed to the new unit (REP MOVSB 5 to 8, declined REP OUTSB 3 to 4); 1,850 unit checks, zero failures. Builds and ctest with `-Werror` (first implementation): all eleven configurations pass (x86-64 Debug, Release, Release at warm-up 0, Clang Debug, ASan/UBSan at both warm-ups, 7/7 each; i386 Debug, Debug at warm-up 0, Release, Release at warm-up 0, 8/8 each; wasm32 on emsdk 3.1.74 5/5 with `rex86_probe` `result=ok`). Fuzz (first implementation), the campaign stopped midway at the user's request: integer i386 1M at the default warm-up (seed 3201) zero mismatches with `vendor_deviations=14`, at warm-up 0 (seed 3202) zero mismatches with 16; x87 1M (seed 3203), SIMD 1M on x86-64 (seed 3204) and 300,000 on i386 (seed 3205), zero mismatches; the 18,396-case trace corpus clean on x86-64 and i386; the long robustness runs have no result (stopped), only ctest's 300-case smoke passing everywhere; unresolved: whether 14 and 16 integer vendor deviations also arise before the change on this machine (AMD Ryzen 5 5600X) was not compared. Benchmark (the table above; three interleaved runs each against `main`, medians): the first implementation set three `Ctx` fields, passed the allowance and `attention` and wrote `StepResult::steps` per instruction; the `StepBudget` implementation keeps one budget on `RunBlock`'s stack, passes `Ctx` one pointer and has string instructions alone report steps (through a destructor on every way out), yet alu and call still regress, so the completion criterion (workloads without REP within the noise) does not hold yet; `string`'s steps per lap went from 53,258 to 569,354 (counting iterations); the x87 and mixed lines were missed by the measuring script's parsing and need measuring again.*

### 이어서 할 일 / Next

1. **성능 회귀를 측정으로 찾는다**: 표본 프로파일이나 디스어셈블리로 `RunBlock`의 핫 루프를 수정 전과 비교한다. 후보는 다음과 같다. 고친 뒤 번갈아 다시 잰다.
   * 명령마다의 `budget.allowance` 저장과 `string_steps` 검사.
   * `ExecuteDecoded`의 인자 하나 증가로 인한 레지스터 압박.
   * `Ctx` 크기 변화.
   * `Cpu::RunUntilStop`의 `in_progress_` 검사.
2. 같은 기계에서 수정 전 `main`의 정수 fuzz를 같은 시드(3201)로 돌려 `vendor_deviations`를 비교한다.
3. 남은 검증: 견고성 Release 20만 건과 ASan 2만 건, `StepBudget` 구현 기준의 빌드 전부. 긴 fuzz는 [#37](https://github.com/reexec/rex86/issues/37)(tag 시점 CI fuzz)로 옮겨 갈 수 있다.
4. 문서: README(목표 8, 현재 수치), 분석 `interpreter-performance.md`(단위 변경, 전후).
5. CI 확인 뒤 사용자 요청 시 머지한다.

*Next: 1 find the performance regression by measurement (sampling profiles or disassembly of `RunBlock`'s hot loop against the version before), the candidates being the per-instruction `budget.allowance` store and `string_steps` check, register pressure from `ExecuteDecoded`'s extra argument, `Ctx`'s size and `Cpu::RunUntilStop`'s `in_progress_` check, re-measuring interleaved after each fix; 2 compare `vendor_deviations` against `main`'s integer fuzz at the same seed (3201) on the same machine; 3 the remaining verification: 200,000 Release and 20,000 ASan robustness cases and every build on the `StepBudget` implementation, the long fuzz possibly moving to #37 (tag-time CI fuzz); 4 documents: README (goal 8, current figures) and the analysis `interpreter-performance.md` (the unit change, before and after); 5 merge on the user's request once CI is checked.*
