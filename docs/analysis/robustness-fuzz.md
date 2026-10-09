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

**코어에서 찾은 결함은 없다.** 이 하네스 이전의 작업들(SingleStepTests, 세 호스트 대조 fuzz, ASan/UBSan CI)이 이미 경계 검사와 폴트 경로를 넓게 다뤘기 때문으로 **추정**한다. 하네스를 만들며 고친 것은 하네스 자신과 빌드의 문제다.

* MSVC가 다이제스트에서 `bool`과 `int`를 `|`로 섞은 곳을 경고 C4805로 막았다.
* 처음 생성기는 케이스 대부분이 첫 명령에서 폴트로 끝났다(300건에 660명령). 온건한/적대적 케이스로 나누고, 명령 수프의 ModRM과 변위를 조정하고, 인출 폴트 뒤 EIP를 옮기도록 해 케이스당 retire 수를 약 70배로 늘렸다.
* libFuzzer 입력이 생성기의 처음 몇 선택에만 쓰여, 변형이 명령 바이트에 닿지 않았다. 앞 16바이트만 생성기에 쓰고 나머지를 EIP의 코드로 넣도록 바꿨다.

***No defect was found in the core**, inferred to be because earlier work (SingleStepTests, three host-comparison fuzzes, the ASan/UBSan CI) already covered the bounds checks and fault paths broadly. What was fixed belongs to the harness and the build: MSVC's C4805 on bool-int packing in the digest; a first generator whose cases mostly ended at the first instruction (660 instructions over 300 cases), raised about seventyfold by benign/hostile cases, ModRM and displacement tuning in the instruction soup and moving EIP after fetch faults; and libFuzzer input that only fed the generator's first choices, now 16 header bytes for the generator and the rest as the code at EIP.*

## 3. 발견 사항: REP의 정지 시간 / Finding: REP stall time

**확인됨(코드 읽기)**: REP 문자열은 한 Step 안에서 끝까지 반복한다. 32비트 주소에서 ECX가 크면 한 명령이 매핑된 연속 영역 크기 / 폭만큼 돈다. 크래시가 아니라 목표 8(반응성)의 문제라 [#32](https://github.com/reexec/rex86/issues/32)로 넘겼다. 하네스는 메모리를 1 MiB 이하로 두어 케이스의 시간을 묶는다.

*Confirmed by reading the code: a REP string runs to completion within one Step, so with 32-bit addressing and a large ECX one instruction covers the contiguous mapped region. Not a crash but a goal 8 (responsiveness) problem, handed to #32; the harness bounds memory at 1 MiB to bound a case's time.*

## 4. 미확정 / Unresolved

* **번역 백엔드**: 없음. 3단계에서 같은 하네스가 차등 비교로 넓힌다.
* **wasm32와 AArch64에서의 긴 실행**: CI의 smoke(300건)만 돈다. 이 환경에 도구가 없다.
* **identity 매핑(`base` = null)**: 하네스는 실제 버퍼만 쓴다. null base는 호스트 주소가 곧 게스트 주소라 보호 구역을 둘 수 없다.

*Unresolved: translation backends (none yet; phase 3 extends the harness differentially); long runs on wasm32 and AArch64 (only CI's 300-case smoke runs there, the tools being absent here); the identity mapping (`base` = null), which the harness cannot guard since guest addresses are host addresses.*
