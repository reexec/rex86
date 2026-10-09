# 견고성 fuzz 결과 / Robustness fuzz results

근거 작업: [#31](https://github.com/reexec/rex86/issues/31) ([설계](../design/20261009-i031-robustness-fuzz.md), [로그](../work-logs/20261009-i031-robustness-fuzz.md)) | 절차: [견고성 fuzz 가이드](../guides/robustness-fuzz.md)

이 문서는 `rex86_robust`와 그 libFuzzer 진입점으로 잰 README 목표 6(견고성과 격리)의 결과를 누적한다. 불변식 I1~I6은 설계 결정 1에 있다.

*This topic accumulates README goal 6 (robustness and sandboxing) as measured by `rex86_robust` and its libFuzzer entry; invariants I1-I6 are in design decision 1.*

## 1. 실행 기록 / Runs

**확인됨**. 측정 호스트는 AMD Ryzen 5 5600X(WSL2 Ubuntu 24.04)와 같은 기계의 Windows 11이다. 2026-10-09, #31 작업 브랜치.

| 구성 | 케이스(시드) | Run | retire된 명령 | 디코더 케이스 | 위반 | 새니타이저 보고 |
|---|---|---|---|---|---|---|
| GCC 13 Release x86-64 | 500,000 (1,000,000~) | 4,753,215 | 71,489,513 | 16,000,000 | 0 | — |
| GCC 13 ASan/UBSan x86-64 | 20,000 (100,000~) | 188,957 | 2,584,581 | 640,000 | 0 | 0 |
| GCC 13 ASan/UBSan x86-64 | 50,000 (2,000,000~) | 476,716 | 6,130,091 | 1,600,000 | 0 | 0 |
| GCC 13 i386 Debug | 10,000 (3,000,000~) | 94,551 | 1,298,591 | 320,000 | 0 | — |
| MSVC 19.50 x86 Release | 20,000 (4,000,000~) | 191,551 | 2,782,542 | 640,000 | 0 | — |
| Clang 18 libFuzzer + ASan/UBSan | 30분, 62,394회(+ 첫 시도 10분 11,828회) | — | — | — | 크래시 0 | 0 |

케이스마다 두 번 돌려 결정성(I6)을 보므로 실제 실행은 표의 두 배다. 하네스 자신의 검사기는 단위 테스트가 일부러 만든 위반(쓰기 금지 페이지 변경, 보호 구역 쓰기, 예산을 넘는 이벤트)을 모두 잡는다.

*Confirmed on an AMD Ryzen 5 5600X (WSL2 Ubuntu 24.04) and Windows 11 on the same machine, 2026-10-09, on #31's branch, as in the table: zero violations and zero sanitizer reports everywhere. Each case runs twice for determinism (I6), so actual executions are double the table. The checkers themselves catch every violation the unit tests produce on purpose (a write-protected page changed, a guard-zone write, an event over budget).*

## 2. 찾은 결함 / Defects found

**#31 작업 중에는 코어에서 찾은 결함이 없었다**(#34에서 하나를 찾았다, 아래). 이 하네스 이전의 작업들(SingleStepTests, 세 호스트 대조 fuzz, ASan/UBSan CI)이 이미 경계 검사와 폴트 경로를 넓게 다뤘기 때문으로 **추정**한다. 하네스를 만들며 고친 것은 하네스 자신과 빌드의 문제다.

* MSVC가 다이제스트에서 `bool`과 `int`를 `|`로 섞은 곳을 경고 C4805로 막았다.
* 처음 생성기는 케이스 대부분이 첫 명령에서 폴트로 끝났다(300건에 660명령). 온건한/적대적 케이스로 나누고, 명령 수프의 ModRM과 변위를 조정하고, 인출 폴트 뒤 EIP를 옮기도록 해 케이스당 retire 수를 약 70배로 늘렸다.
* libFuzzer 입력이 생성기의 처음 몇 선택에만 쓰여, 변형이 명령 바이트에 닿지 않았다. 앞 16바이트만 생성기에 쓰고 나머지를 EIP의 코드로 넣도록 바꿨다.

***No core defect was found during #31** (one was found during #34, below), inferred to be because earlier work (SingleStepTests, three host-comparison fuzzes, the ASan/UBSan CI) already covered the bounds checks and fault paths broadly. What was fixed belongs to the harness and the build: MSVC's C4805 on bool-int packing in the digest; a first generator whose cases mostly ended at the first instruction (660 instructions over 300 cases), raised about seventyfold by benign/hostile cases, ModRM and displacement tuning in the instruction soup and moving EIP after fetch faults; and libFuzzer input that only fed the generator's first choices, now 16 header bytes for the generator and the rest as the code at EIP.*

### #34에서 찾은 결함 / Found during #34

* **확인됨(ASan, 2026-10-09)**: 시드 5046451(캐시를 늘 켠 Release 빌드의 20만 건 실행)에서 스택 버퍼 넘침. `ExecuteMmx`가 MM 레지스터를 쓰는 명령을 모두 받아, `Features::sse2`가 켜진 케이스에서 SSE2의 `CVTPD2PI mm, m128`이 들어오자 8바이트 버퍼에 16바이트를 읽었다. 1b(#29)에서 들어간 결함이며 디코드 캐시와는 관계없다(결과는 캐시 유무와 무관하고, 그 시드 범위를 캐시 빌드로만 돌렸을 뿐이다). `ExecuteMmx`가 MMX와 SSE 집합만 받고, MM과 XMM 원본 읽기가 너무 큰 피연산자를 거절하도록 고쳤다. `sse2`는 범위 밖이므로 그런 명령은 #UD다. 단위 테스트로 고정했다.

*Confirmed (ASan, 2026-10-09): seed 5046451, in a 200,000-case run of a cache-always Release build, overflowed a stack buffer: `ExecuteMmx` took every instruction with an MM register, so with `Features::sse2` on, SSE2's `CVTPD2PI mm, m128` read 16 bytes into an 8-byte buffer. A defect from 1b (#29), unrelated to the decode cache (results do not depend on it; that seed range simply ran on a cache build). Fixed by `ExecuteMmx` taking the MMX and SSE sets only and the MM/XMM source readers refusing oversized operands; with `sse2` out of scope such instructions are #UD; pinned by a unit test.*

## 3. 발견 사항: REP의 정지 시간 / Finding: REP stall time

**확인됨(코드 읽기)**: REP 문자열은 한 Step 안에서 끝까지 반복한다. 32비트 주소에서 ECX가 크면 한 명령이 매핑된 연속 영역 크기 / 폭만큼 돈다. 크래시가 아니라 목표 8(반응성)의 문제라 [#32](https://github.com/reexec/rex86/issues/32)로 넘겼다. 하네스는 메모리를 1 MiB 이하로 두어 케이스의 시간을 묶는다.

*Confirmed by reading the code: a REP string runs to completion within one Step, so with 32-bit addressing and a large ECX one instruction covers the contiguous mapped region. Not a crash but a goal 8 (responsiveness) problem, handed to #32; the harness bounds memory at 1 MiB to bound a case's time.*

**해소됨(2026-10-10, [#32](https://github.com/reexec/rex86/issues/32))**: REP 문자열은 이제 반복마다 한 단계로 세고, 예산, 정지 요청, 대기 인터럽트가 반복 사이에서 명령을 멈춘다. 그래서 `Run(budget)`의 일은 메모리 크기와 상관없이 budget 단계로 묶인다(단위 테스트 `rep_budget_test.cpp`). 하네스의 1 MiB 상한은 시드별 재현을 지키려고 그대로 둔다.

*Resolved (2026-10-10, #32): a REP string now counts one step per iteration, and the budget, a stop request or a pending interrupt stops it between iterations, so `Run(budget)`'s work is bounded at budget steps whatever the memory size (unit test `rep_budget_test.cpp`). The harness keeps its 1 MiB bound so that per-seed reproduction holds.*

## 4. 미확정 / Unresolved

* **번역 백엔드**: 없음. 3단계에서 같은 하네스가 차등 비교로 넓힌다.
* **wasm32와 AArch64에서의 긴 실행**: CI의 smoke(300건)만 돈다. 이 환경에 도구가 없다.
* **identity 매핑(`base` = null)**: 하네스는 실제 버퍼만 쓴다. null base는 호스트 주소가 곧 게스트 주소라 보호 구역을 둘 수 없다.

*Unresolved: translation backends (none yet; phase 3 extends the harness differentially); long runs on wasm32 and AArch64 (only CI's 300-case smoke runs there, the tools being absent here); the identity mapping (`base` = null), which the harness cannot guard since guest addresses are host addresses.*
