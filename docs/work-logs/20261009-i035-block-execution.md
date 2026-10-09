# #35 작업 로그 : 블록 단위 실행 / #35 work log : block-level execution

이슈: [#35](https://github.com/reexec/rex86/issues/35) | 설계: [20261009-i035](../design/20261009-i035-block-execution.md) | 지시서: [20261009-i035](../work-orders/20261009-i035-block-execution.md)

## 2026-10-09

- **#34 위에 쌓음**: #34는 사용자 결정으로 머지하지 않은 채 이 브랜치가 그 위에서 시작했다.
- **측정 먼저**:
  - rdtsc 단계 탐침(임시 사본)으로는 "조회" 단계가 명령당 100틱 넘게 나왔지만, `Lookup`만 따로 잰 마이크로벤치마크는 5.6 ns였다. 직렬화하지 않은 rdtsc의 오귀속으로 판단하고 버렸다.
  - gprof의 줄 단위 모드는 "somebody miscounted"로 깨졌다. 그래서 표본 프로파일러를 임시로 만들었다(Linux `LD_PRELOAD`+`SIGPROF`, Windows 일시정지+DbgHelp).
  - GCC에서 `std::optional<DecodedInstruction>` 지역 변수의 0 채우기(`rep stos` 1,144바이트)가 31.9%였다. 게이트 해시 조회는 약 6%였다.
- **시도와 결과**(전후를 번갈아 잼):
  - 블록 루프만 넣었을 때: GCC는 잡음 범위 안이었다.
  - 미스 경로 분리: GCC alu 14.6 → 26.4. MSVC는 이것만으로 3~8% 느려졌고, 블록 루프를 더하면 기준선보다 5~10% 빨랐다.
  - 세그먼트를 처음 쓸 때만 보관하는 방식: 잡음 범위 안이어서 되돌렸다.
  - MSVC 프로파일(새 Windows 표본기)에서 `SlotOf` 16%와 `StepResult` 복사 8.5%가 나왔다. 둘을 고치니 GCC alu 26 → 30, MSVC alu 15 → 21.
  - 분기 뒤처리의 싼 검사를 앞으로 옮긴 것은 같은 의미이고 남겼다.
- **구현**:
  - `ExecuteDecoded`/`StepMiss`(noinline)/`StepOne`, 결과 제자리 쓰기.
  - `interp::RunBlock`과 `BlockLimits`/`BlockResult`/`GateFilter`.
  - `Cpu`: `attention_`(atomic_ref), `RefreshAttention`, 게이트 필터, 블록 단위 루프.
  - `SlotOf` constexpr 표.
  - `WriteVirtual`의 낡은 주석(인터프리터에 낡은 블록이 없다)을 고쳤다.
- **단위 테스트** `block_test.cpp`: 캐시가 없을 때와 있을 때 각각 아홉 경우.
  - 게이트로 흘러듦, 필터 거짓 양성, 해제 뒤 재구성.
  - 명령 중 인터럽트, IF가 꺼진 동안의 대기와 STI 그림자, 명령 중 정지 요청.
  - 블록 안 예산, 블록 안 SMC, 블록 중간 폴트.
- **검증**:
  - 정수 fuzz i386: 캐시 상시 100만 건, 기본 예열 20만 건. x87 fuzz 200만 건. SIMD fuzz x86-64 200만 건, i386 50만 건. 모두 불일치 0.
  - 견고성: Release 캐시 상시 20만 건과 기본 10만 건, ASan 캐시 상시 2만 건. 위반 0.
  - trace 묶음 18,396건: x86-64와 i386 모두 불일치 0.
  - 처음 캠페인 스크립트가 fuzz 출력의 마지막 줄만 남겨 요약을 잃었다. fuzz는 요약 줄을 잡도록 다시 돌렸다.
- **빌드와 테스트**(`-Werror`):
  - 단위 테스트 checks 1,182, 일곱 빌드 모두 통과: x86-64 Debug, Release, 새니타이저, 캐시 상시와 그 ASan, i386 Debug와 캐시 상시.
  - MSVC x86 Debug와 Release ctest 5/5.
- **벤치마크**(번갈아 세 번씩, 중앙값):
  - GCC mixed 9.11 → 12.50, alu 15.04 → 29.93.
  - MSVC mixed 7.25 → 9.07, alu 14.23 → 20.83.
  - MSVC의 전 수치가 #34 때 같은 코드의 기록(10.23)보다 낮다. 원인은 호스트 상태로 보이나 미확정이고, 배율만 비교한다(분석 2.2절).
- **남은 것**:
  - 저장 경로와 문자열 명령(#32와 겹침).
  - 의미 실행의 피연산자 해석.
  - 3단계 번역 백엔드.

*Stacked on #34, left unmerged by the user's decision. Measured first: rdtsc phase probes (in a scratch copy) put the "lookup" phase above 100 ticks per instruction while a microbenchmark of `Lookup` alone gave 5.6 ns, so that was discarded as misattribution by unserialized rdtsc; gprof's line mode broke ("somebody miscounted"), so scratch sampling profilers were built (Linux `LD_PRELOAD` plus `SIGPROF`, Windows suspension plus DbgHelp); under GCC, zeroing the `std::optional<DecodedInstruction>` local (`rep stos`, 1,144 bytes) was 31.9% and the gate hash lookup about 6%. Tried and measured, interleaved: the block loop alone was within the noise under GCC; splitting off the miss path took GCC's alu from 14.6 to 26.4, while under MSVC it alone was 3-8% slower and the block loop added made it 5-10% faster than the baseline; keeping segments only on their first write was within the noise and reverted; the MSVC profile (the new Windows sampler) showed `SlotOf` at 16% and the `StepResult` copy at 8.5%, and fixing both took GCC's alu from 26 to 30 and MSVC's from 15 to 21; testing the branch epilogue's cheap part first means the same and stayed. Implemented `ExecuteDecoded`/`StepMiss` (noinline)/`StepOne` with results in place, `interp::RunBlock` with `BlockLimits`/`BlockResult`/`GateFilter`, `Cpu`'s `attention_` (atomic_ref), `RefreshAttention`, the gate filter and per-block loop, the constexpr `SlotOf` table, and fixed `WriteVirtual`'s stale comment (the interpreter having no stale blocks). Unit tests in `block_test.cpp`, nine cases each without and with the cache: falling into a gate, a filter false positive, rebuilding after unregistering, an interrupt during an instruction, waiting with IF clear and STI's shadow, a stop request during an instruction, the budget, SMC and a fault mid-block. Verified: integer fuzz on i386 (1M cache-always, 200,000 at the default warm-up), x87 2M, SIMD 2M on x86-64 and 500,000 on i386, all with zero mismatches; robustness at 200,000 cache-always and 100,000 default Release cases and 20,000 cache-always ASan cases, zero violations; the 18,396-case trace corpus clean on x86-64 and i386; the first campaign script kept only each fuzz's last line and lost the summaries, so the fuzzes were rerun capturing them. Builds and tests with `-Werror`: 1,182 unit checks on all seven builds (x86-64 Debug, Release, sanitizer, cache-always and its ASan, i386 Debug and cache-always), MSVC x86 Debug and Release ctest 5/5. Benchmark (three interleaved runs each, medians): GCC mixed 9.11 to 12.50 and alu 15.04 to 29.93, MSVC mixed 7.25 to 9.07 and alu 14.23 to 20.83; MSVC's before figures are below #34's record for the same code (10.23), apparently host state but unresolved, so only ratios compare (analysis 2.2). Remaining: the store path and string instructions (overlapping #32), operand resolution in the semantics, phase 3's backends.*
