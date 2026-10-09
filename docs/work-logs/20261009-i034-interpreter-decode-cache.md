# #34 작업 로그 : 인터프리터 빠른 경로 / #34 work log : the interpreter fast path

이슈: [#34](https://github.com/reexec/rex86/issues/34) | 설계: [20261009-i034](../design/20261009-i034-interpreter-decode-cache.md) | 지시서: [20261009-i034](../work-orders/20261009-i034-interpreter-decode-cache.md)

## 2026-10-09

- **측정 먼저**: perf와 valgrind가 없고(sudo 비밀번호 필요) gprof로 쟀다.
  - 인라인을 끈 빌드의 호출 횟수: 명령마다 `AllHave` 34회, `Read8` 15회.
  - 인라인을 켠 빌드의 비중: 인출 약 32%, Zydis 디코드 약 31%, `Step` 17%, 의미 약 10%.
  - 기준선은 같은 기계에서 다시 쟀다: GCC Release mixed 3.60 MIPS, MSVC Release 2.28 MIPS.
- **설계 중 바로잡은 것**: 처음 설계는 세대(epoch)를 `Cpu`마다 두려 했다.
  - 두 소비자는 게스트 스레드마다 `Cpu`를 두고 메모리를 공유한다. 이 경우 한 Cpu의 저장 뒤 다른 Cpu가 `kTranslated`를 다시 세우면, 세 번째 Cpu의 낡은 항목이 통과한다.
  - 그래서 세대를 `PageAttributeTable`에 두었다(`kTranslated`가 지워질 때 + 1, 공개 조회 `Generation`). `WriteVirtual`과 `InvalidateCode`는 바뀌지 않는다.
- **구현**:
  - `PageAttributeTable`: `Update`가 세대를 관리한다.
  - `src/interp/decode_cache.{h,cpp}`: 직접 사상 4,096칸. 저장소는 초기화하지 않는 지연 할당이고, 칸은 `Claim`할 때 생성한다.
  - `interp::Step`: 캐시 인자를 받는다. 적중하면 인출과 디코드를 건너뛰고, 놓치면 묶음 인출(`Fetch`) 뒤 칸에 바로 디코드한다.
  - `Cpu`: 64명령 예열 뒤 캐시를 만든다(`REX86_DECODE_CACHE_WARMUP`). 게이트가 없으면 조회를 건너뛴다. 이동만 되며, `EngineMemoryBytes`를 더했다.
  - 벤치마크의 메모리 줄을 고쳤다(세대 표와 `engine_bytes`).
  - 견고성 하네스의 I4: 코어가 실행 가능한 페이지에 `kTranslated`를 세우는 것을 허용했다(설계 결정 5).
- **CI**: 새니타이저 작업을 `REX86_DECODE_CACHE_WARMUP=0`으로 돌린다. 다른 작업은 기본 예열이므로 두 모드가 모두 지켜진다.
- **단위 테스트** `decode_cache_test.cpp`:
  - 세대 규칙, 적중, 같은 페이지의 SMC 둘, 두 Cpu의 공유.
  - CS base가 다른 같은 선형 주소, limit 축소와 실행 권한 회수 뒤의 폴트.
  - `Cpu`를 통한 `InvalidateCode`와 이동.
- **찾은 결함**(설계 결정 5의 "결과 불변" 검증 중): 캐시를 늘 켠 Release 빌드의 견고성 20만 건이 시드 5046451에서 "stack smashing"으로 죽었다.
  - ASan으로 보니 `ExecuteMmx`가 SSE2의 `CVTPD2PI mm, m128`(케이스가 `sse2`를 켰을 때)을 받아 8바이트 버퍼에 16바이트를 읽었다.
  - 1b(#29)의 결함이다. 결과가 캐시와 무관하므로 디코드 캐시와도 관계없다.
  - `ExecuteMmx`가 MMX/SSE 집합만 받고, MM/XMM 원본 읽기가 너무 큰 피연산자를 거절하도록 고쳤다. 단위 테스트로 고정했다. 같은 범위를 다시 돌려 위반 0이었다.
- **결과 불변 검증**(캐시를 늘 켠 빌드):
  - 정수 fuzz(i386) 100만 건, x87 fuzz 200만 건, SIMD fuzz x86-64 200만 건과 i386 50만 건: 불일치 0.
  - 견고성: ASan 2만 건, Release 20만 건(수정 뒤). 위반 0.
  - trace 묶음 18,396건 재생 불일치 0.
- **벤치마크**(단독 측정, 같은 기계):
  - GCC Release mixed 3.60 → 9.53 MIPS(2.6배). MSVC Release 2.28 → 10.23 MIPS(4.5배). 표는 [분석](../analysis/interpreter-performance.md) 2.1절에 있다.
  - 처음 MSVC를 WSL의 긴 실행과 겹쳐 쟀을 때는 5.23이 나왔다. 그 값은 버리고 단독으로 다시 쟀다.
- **빌드와 ctest**(`-Werror`): x86-64 Debug와 Release 7/7, i386 Debug 8/8, GCC ASan/UBSan(기본 예열) 7/7과 예열 0 7/7, MSVC x86 Debug 5/5. 단위 테스트 checks 1,118.
- **남은 것**:
  - 남은 병목은 `Step`의 상태 저장과 분기, 저장 경로의 검사다(분석 2.1절).
  - 블록 단위 실행이나 3단계 번역 백엔드가 다음 후보다.
  - 소비자가 이미 실행된 코드를 고칠 때 `InvalidateCode`를 부르는지는 통합 때 확인한다.

*Measured first with gprof (no perf or valgrind without a sudo password): 34 `AllHave` and 15 `Read8` calls per instruction without inlining; with it, fetching about 32%, Zydis about 31%, `Step` 17%, semantics about 10%; baselines re-measured on the same machine (GCC mixed 3.60, MSVC 2.28). Corrected in design: per-`Cpu` epochs would let a third Cpu's stale slots pass once another Cpu re-sets `kTranslated` after a store, since both consumers share memory across per-thread Cpus, so the generations live in `PageAttributeTable` (+1 when `kTranslated` clears, public `Generation`), leaving `WriteVirtual` and `InvalidateCode` unchanged. Implementation: `PageAttributeTable::Update`, the 4,096-slot `decode_cache` (uninitialized lazy storage, slots constructed at `Claim`), `interp::Step` with the cache (a hit skips fetch and decode; a miss bulk-fetches and decodes straight into the slot), `Cpu` making its cache after a 64-instruction warm-up (`REX86_DECODE_CACHE_WARMUP`), skipping the gate lookup without gates, move-only, with `EngineMemoryBytes`; the benchmark's memory line fixed; the robustness I4 allowing `kTranslated` set on executable pages. CI's sanitizer job runs with the warm-up at 0, the others at the default, so both modes stay guarded. Unit tests in `decode_cache_test.cpp` cover the generation rule, hits, two same-page SMC cases, two Cpus sharing memory, another CS base, faults after a limit cut or execute revocation, and `InvalidateCode` and moves through `Cpu`. Found while checking that results do not change: a 200,000-case robustness run of a cache-always Release build died of stack smashing at seed 5046451; ASan showed `ExecuteMmx` taking SSE2's `CVTPD2PI mm, m128` (in a case with `sse2` on) and reading 16 bytes into 8, a 1b (#29) defect unrelated to the cache; fixed by limiting `ExecuteMmx` to the MMX/SSE sets and making the MM/XMM source readers refuse oversized operands, pinned by a unit test, and the range rerun clean. Unchanged results on cache-always builds: 1M integer (i386), 2M x87, 2M plus 500,000 SIMD cases with zero mismatches, 20,000 ASan and 200,000 Release robustness cases with zero violations, the 18,396-case trace corpus clean. Benchmark, measured alone: GCC mixed 3.60 to 9.53 (2.6x), MSVC 2.28 to 10.23 (4.5x); an MSVC run overlapping a WSL campaign gave 5.23 and was discarded. Builds and ctest with `-Werror` all pass (x86-64 Debug and Release, i386, GCC ASan/UBSan at both warm-ups, MSVC x86 Debug), 1,118 unit checks. Remaining: `Step`'s state saving and dispatch and the store-path checks (analysis 2.1), with block-level execution or phase 3's backends next; whether consumers call `InvalidateCode` when changing code that ran is checked at integration.*
