# #32 작업 로그 : REP 문자열 반복을 실행 예산에 세기 / #32 work log : counting REP string iterations against the budget

이슈: [#32](https://github.com/reexec/rex86/issues/32) | 설계: [20261010-i032](../design/20261010-i032-rep-string-budget.md) | 지시서: [20261010-i032](../work-orders/20261010-i032-rep-string-budget.md)

## 2026-10-10 (첫 구현 / first implementation)

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
### 이어서 할 일(첫 구현 시점) / Next (at the first implementation)

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

## 2026-10-10 (이어서 / continued)

호스트: 클라우드 VM, Intel Xeon 2.80 GHz 4코어, Ubuntu 24.04, GCC 13.3, Clang 18.1.3. 앞 절의 기계(AMD Ryzen 5 5600X)와 다르다.

- **성능 회귀를 측정으로 찾음**:
  - 이 VM의 벽시계는 같은 바이너리 다섯 번 사이에서도 10% 넘게 흔들려서, cachegrind의 호스트 명령 수(`Ir`)로 판단했다. 두 프레임과 여섯 프레임 실행의 차를 단계 수로 나눈 한계값이다.
  - 앞 절의 `StepBudget` 구현은 단계당 16개 늘었다(alu 503.6 → 519.8). callgrind로 보면 `RunBlock` 자신이 14, `ExecuteDecoded`가 2였다.
  - 디스어셈블리: 명령마다 허용을 계산해 쓰고 `string_steps`를 다시 읽는 몫, 그리고 `StepOne`의 일곱째 인자가 스택으로 넘어가며(`sub`/`push`/`pop`) 레지스터를 밀어내 `features`와 `environment`를 스택에서 다시 읽는 몫이었다.
  - 고침 1: `StepBudget`을 `{limit, used, attention}`으로 바꾸고 `used`를 블록의 단계 계수기로 썼다. `RunBlock`은 retire한 명령마다 하나 올리고, 문자열 명령은 retire하면 마지막을 뺀 반복 수, retire하지 않으면 끝낸 반복 수를 더한다. 16 → 6.
  - 고침 2: 예산 포인터를 인자 대신 `StepResult::budget`에 실었다. `RunBlock`이 이미 모든 명령에 참조로 넘기는 구조체다. 반환 전에 null로 지워 매달린 포인터가 나가지 않게 했다. 6 → 2(alu 505.8, call 493.1, memory 492.8, x87 1048.7; 수정 전 503.6, 490.8, 490.5, 1046.3).
  - 남은 2개는 `ExecuteDecoded`가 포인터를 `Ctx`로 옮기는 몫이다. 0.4~0.5%로 잡음 범위 안이라 두었다.
- **부수 수정**: Clang 18 `-O2` + UBSan(libFuzzer 구성)에서 단위 테스트가 `GuestMemory::HostPointer`의 null 포인터 산술(정체 매핑의 null base)로 멈췄다. #32와 무관한 기존 문제이고, CI의 libFuzzer 작업은 단위 테스트를 돌리지 않아 드러나지 않았다. 주소를 정수로 더하도록 고쳤다. **미확정**: `Read8` 같은 접근자도 null base에서 `base_[address]`를 쓴다. 정체 매핑을 실제로 쓰는 호스트(32비트)에서만 닿는 경로라 이 작업에서는 고치지 않았다.
- **빌드와 ctest**(`-Werror`, 최종 코드): 12개 구성이 모두 통과했다.
  - x86-64 Debug, Release, Release 예열 0, Clang Debug, ASan/UBSan 기본 예열과 예열 0: 각 7/7.
  - i386 Debug, Debug 예열 0, Release, Release 예열 0: 각 8/8.
  - Clang RelWithDebInfo + libFuzzer + ASan/UBSan: 7/7(위 수정 뒤).
  - wasm32(emsdk 3.1.74): 5/5, `rex86_probe` `result=ok`.
- **fuzz와 대조**(최종 코드, 모두 불일치 0):

  | 대상 | 빌드 | 건수 | 시드 | 결과 |
  |---|---|---|---|---|
  | 정수 | i386 Release | 1,000만 | 3201 | `mismatches=0 vendor_deviations=0` |
  | 정수 | i386 Release 예열 0 | 1,000만 | 3202 | `mismatches=0 vendor_deviations=0` |
  | x87 | x86-64 Release | 2,000만 | 3203 | `mismatches=0`, SDM 이탈 123,962(0.62%), 허용 22,742(2 ulp 0) |
  | SIMD | x86-64 Release | 2,000만 | 3204 | `mismatches=0`, 허용 438,397 |
  | SIMD | i386 Release | 500만 | 3205 | `mismatches=0`, 허용 109,422 |
  | trace 묶음 | x86-64, i386 Release | 18,396 | | 각 불일치 0 |
  | 견고성 | x86-64 Release | 50만 | 1000000 | `violations=0 result=ok` |
  | 견고성 | ASan/UBSan Debug | 5만 | 2000000 | `violations=0 result=ok` |
  | libFuzzer | Clang ASan/UBSan | 30분, 159,783회 | | 크래시 0, cov 3319 |
  | SST real mode | x86-64 Release `--execute` | 1,758,700 | | 1,741,900 실행, 불일치 0 |

  - 앞 절의 미확정(`vendor_deviations` 14와 16): 이 값은 AMD만의 폴트 순서(Zen 3의 BOUND, Zen 5의 CMPS, [분석](../analysis/integer-host-comparison.md))를 센다. Intel인 이 호스트에서는 두 시드 모두 0이다. AMD 기계가 없어 수정 전 `main`과 같은 시드로 비교하지는 못했다. 다만 이 수는 코어가 아니라 호스트의 폴트 순서에서 나오고, 코어 쪽 결과는 불일치 0이므로 #32의 회귀로 보지 않는다.
  - x87의 SDM 이탈 0.62%는 분석에 적힌 두 제조사 공통의 이탈(마스크 안 된 #IA 비교, 0.63%)과 같다.
- **벤치마크**(이 VM, 수정 전 `05f4f86`과 번갈아 다섯 번씩, 중앙값, MIPS): alu 17.31 → 16.81, memory 19.53 → 19.52, call 19.31 → 19.41, x87 7.54 → 7.55. `string`은 단위가 달라 바퀴당으로 보면 호스트 명령 152.3M → 163.7M(+7.5%, 반복마다의 검사), 벽시계는 초당 68.1 → 72.6바퀴. 완료 조건(REP가 아닌 워크로드는 잡음 범위 안)을 지킨다. 표와 해설은 [분석](../analysis/interpreter-performance.md) 2.3절에 적었다.
- **문서**: 설계 결정 2에 구현 형태(측정 뒤 갱신)를 더했다. README 목표 8과 현재 수치, 분석 `interpreter-performance.md` 2.3절.

*Host: a cloud VM, Intel Xeon 2.80 GHz with 4 cores, Ubuntu 24.04, GCC 13.3, Clang 18.1.3, not the previous section's machine (AMD Ryzen 5 5600X). Finding the regression by measurement: this VM's wall clock varies by more than 10% across five runs of one binary, so cachegrind's host instruction count (`Ir`) decided, as the marginal value between two- and six-frame runs per step. The previous section's `StepBudget` implementation cost 16 more per step (alu 503.6 to 519.8), 14 in `RunBlock` itself and 2 in `ExecuteDecoded` by callgrind; the disassembly showed the per-instruction allowance computed and written and `string_steps` read back, and `StepOne`'s seventh argument going on the stack (`sub`/`push`/`pop`) and pushing registers out so that `features` and `environment` were reloaded from the stack. Fix 1: `StepBudget` became `{limit, used, attention}` with `used` as the block's step counter; `RunBlock` raises it for each retired instruction, and a string instruction adds the iterations beyond its last when it retires or every iteration it completed when it does not: 16 to 6. Fix 2: the budget pointer rides in `StepResult::budget` instead of an argument, the struct `RunBlock` already passes every instruction by reference, cleared to null before returning so no dangling pointer leaves: 6 to 2 (alu 505.8, call 493.1, memory 492.8, x87 1048.7 against 503.6, 490.8, 490.5 and 1046.3 before). The remaining 2 are `ExecuteDecoded` copying the pointer into `Ctx`, 0.4-0.5% and within the noise, so they stay. Side fix: with Clang 18 at `-O2` and UBSan (the libFuzzer configuration) the unit tests stopped on null-pointer arithmetic in `GuestMemory::HostPointer` (the identity mapping's null base), an existing problem unrelated to #32 that CI's libFuzzer job, which runs no unit tests, did not reveal; the address is now added as an integer. Unresolved: accessors such as `Read8` also use `base_[address]` with a null base, a path reached only on hosts that use the identity mapping (32-bit), not fixed in this task. Builds and ctest with `-Werror` on the final code: all twelve configurations pass (x86-64 Debug, Release, Release at warm-up 0, Clang Debug, ASan/UBSan at both warm-ups, 7/7 each; i386 Debug, Debug at warm-up 0, Release, Release at warm-up 0, 8/8 each; Clang RelWithDebInfo with libFuzzer and ASan/UBSan 7/7 after the fix; wasm32 on emsdk 3.1.74 5/5 with `rex86_probe` `result=ok`). Fuzz and comparison on the final code, all with zero mismatches, in the table above: integer i386 10M at each warm-up (seeds 3201, 3202) with zero vendor deviations; x87 20M (seed 3203) with 123,962 SDM deviations (0.62%) and 22,742 tolerated (none at 2 ulps); SIMD 20M on x86-64 (seed 3204) and 5M on i386 (seed 3205); the 18,396-case trace corpus on x86-64 and i386; robustness 500,000 Release and 50,000 ASan/UBSan cases with zero violations; libFuzzer 30 minutes, 159,783 runs, no crash, cov 3319; SST real mode `--execute` 1,741,900 of 1,758,700 executed with zero mismatches. The previous section's unresolved `vendor_deviations` of 14 and 16: they count AMD's own fault order (Zen 3's BOUND, Zen 5's CMPS, [analysis](../analysis/integer-host-comparison.md)); on this Intel host both seeds give 0. With no AMD machine, `main` before the change was not compared at the same seed, but the count comes from the host's fault order rather than the core, and the core's side has zero mismatches, so it is not taken as a #32 regression. The x87's 0.62% SDM deviations match the analysis's deviation both vendors share (unmasked-#IA compares, 0.63%). Benchmark (this VM, five interleaved runs each against `05f4f86`, medians, MIPS): alu 17.31 to 16.81, memory 19.53 to 19.52, call 19.31 to 19.41, x87 7.54 to 7.55; `string`, in another unit, per lap from 152.3M to 163.7M host instructions (+7.5%, the per-iteration check) while its wall clock went from 68.1 to 72.6 laps a second; the completion criterion (workloads without REP within the noise) holds, with the table and discussion in section 2.3 of the [analysis](../analysis/interpreter-performance.md). Documents: the design's decision 2 gained the implementation form (updated after measuring); README goal 8 and current figures; section 2.3 of the analysis `interpreter-performance.md`.*

### 이어서 할 일 / Next

1. CI(다섯 호스트와 libFuzzer 작업) 확인.
2. 사용자 요청 시 머지한다.

*Next: 1 check CI (the five hosts and the libFuzzer job); 2 merge on the user's request.*
