# #13 작업 로그 : 인터프리터 2차 / #13 work log : interpreter increment 2

이슈: [#13](https://github.com/reexec/rex86/issues/13) | 설계: [20261007-i013](../design/20261007-i013-interpreter-increment-2.md) | 지시서: [20261007-i013](../work-orders/20261007-i013-interpreter-increment-2.md)

## 2026-10-07

- **구현**: `src/interp/exec.h`(공용 dispatch 인터페이스), `exec_arith.cpp`(시프트/회전, SHLD/SHRD, MUL/IMUL/DIV/IDIV와 `kDivide` 폴트, BT/BTS/BTR/BTC(메모리 비트 베이스 포함), BSF/BSR, SETcc, ENTER/LEAVE, XLAT), `exec_strings.cpp`(MOVS/STOS/LODS/SCAS/CMPS + REP/REPE/REPNE, 한 Step 안에서 반복 완료). `interpreter.cpp`의 default 분기가 `ExecuteExtended`로 위임한다.
- **러너 보강**
  - **RM32/f_umask 극성 확정**: 마스크 비트 1 = 정의된 비트(비교), 0 = 미정의(무시). 1차의 비교는 극성이 반대였는데 1차 명령군은 마스크가 거의 없어 우연히 성립했었다. `80386.csv`의 `f_umask`(열 39)를 로드해 RM32가 없는 파일(BT, SHLD, BSF 등)에도 적용한다.
  - 자기 HLT를 덮어쓰는 테스트와 ECX가 큰 a32 REP(리그가 중간에 중단한 부분 상태 기록)를 `skipped_unrepresentable`로, 하드웨어가 SDM과 달리 #DE 없이 완료하는 IDIV 음수 오버플로를 `skipped_hw_quirk`로 분리.
- **이번에 실측으로 도출한 386 동작** (전체는 [analysis](../analysis/singlesteptests-386ex-deviations.md) 2b절)
  - 시프트 count > 폭의 CF: count가 폭의 배수면 SHR=부호 비트/SHL=최하위 비트, 아니면 0 (복제 가설은 반례로 기각).
  - 16비트 SHLD/SHRD의 count > 16: 결과 = 소스만의 ROL16/ROR16(count−16), 목적지 무시. CF 공식은 count ≤ 16과 동일.
  - SHR count > 1의 OF = 0 (per-step 계산의 귀결).
  - BT 계열 OF = ROR(dst, index)의 OF, 다른 플래그 보존 — f_umask가 가리므로 코어엔 미반영(기록만).
  - IDIV 음수 오버플로 비폴트 완료(AL=0x80, AH=q=−128 기준 나머지): 술어 미확정, 코어는 SDM #DE 유지.
- **버그 수정**: 문자열 소스 세그먼트가 ES override(`es movsb`)를 무시하던 것(소스 = (E)SI 피연산자의 세그먼트로), 32비트 SHLD carry의 `>> 64` 미정의 동작.
- **검증**
  - 단위 테스트 11개 블록 추가(시프트 경계, RCR, MUL/IMUL/IDIV와 kDivide EIP 보존, REP MOVS/REPNE SCAS, BTS 비트 베이스, BSF 0, SETcc, ENTER/LEAVE). `rex86_unit_tests` checks 310 failures 0.
  - **SST 실행 비교(최종)**: 941 파일 1,758,700 테스트 — **executed=1,465,515, passed=1,465,515, mismatches=0** (1차 838,271 대비 +627,244). skipped_unimplemented=129,985(세그먼트 적재, far 제어 흐름, BCD, x87 등), skipped_exception=150,304, skipped_boundary=6,000, skipped_unrepresentable=4, skipped_hw_quirk=6,892.
  - 로컬 Windows x86(MSVC, 경고를 오류로) 빌드 깨끗, `ctest` 2/2. 나머지 호스트는 push 시 CI.
- **남은 것**: 3차 증분(세그먼트 적재, far 제어 흐름, BCD, 예외 의미 비교), x87.

*Implementation: the shared dispatch interface in `exec.h`, `exec_arith.cpp` (shifts/rotates, SHLD/SHRD, MUL/IMUL/DIV/IDIV with the kDivide fault, the BT family with the memory bit base, BSF/BSR, SETcc, ENTER/LEAVE, XLAT) and `exec_strings.cpp` (the string instructions with REP/REPE/REPNE completing within one Step), reached through `ExecuteExtended` from the dispatch's default case. Runner: the RM32/f_umask polarity was pinned down — a SET bit marks a DEFINED bit (increment 1's comparison had it inverted and passed only because its groups carried almost no masks) — with `80386.csv`'s `f_umask` (column 39) loaded for files shipping no RM32; tests that overwrite their own trailing HLT and large-count a32 REPs (the rig records a mid-string interruption) are skipped as unrepresentable, and the IDIV negative-overflow completions as the hardware quirk. Newly measured 386 behavior (analysis section 2b): the shift-count-above-width CF rule (sign/low bit at width multiples, zero otherwise; the replication hypothesis fell to counterexamples), 16-bit double shifts above count 16 rotating the source alone, SHR's OF zero above count 1, the BT family's rotate-style OF (recorded, not adopted — f_umask hides it), and the unresolved IDIV predicate (the core keeps the SDM's #DE). Bug fixes: the string source segment ignored an ES override, and the 32-bit SHLD carry used an undefined shift by 64. Verification: eleven new unit blocks, checks 310 failures 0; the final SST execute-and-compare over 941 files and 1,758,700 tests — **1,465,515 executed, 1,465,515 passed, zero mismatches** (+627,244 over increment 1), with 129,985 still unimplemented (segment loads, far control flow, BCD, x87, ...), 150,304 exception, 6,000 boundary, 4 unrepresentable and 6,892 quirk skips; the local Windows x86 build is clean with ctest 2/2 and CI checks the other hosts on push. Remaining: increment 3 (segment loads, far control flow, BCD, exception semantics) and x87.*
