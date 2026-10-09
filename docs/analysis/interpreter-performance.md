# 인터프리터 성능 기준선 / The interpreter's performance baseline

근거 작업: [#27](https://github.com/reexec/rex86/issues/27) ([설계](../design/20261009-i027-benchmark-harness.md), [로그](../work-logs/20261009-i027-benchmark-harness.md)) | 절차: [벤치마크 가이드](../guides/benchmark.md) | 기준 CPU: [대상 기판의 CPU](../kb/target-board-cpus.md) 3절

이 문서는 벤치마크 하네스 `rex86_bench`로 잰 코어의 처리량을 호스트와 시점을 밝혀 누적한다. 수치는 **그 호스트에서만** 뜻이 있다. 다른 호스트의 수치와 비교하려면 같은 워크로드를 그 호스트에서 다시 재야 한다. 모든 측정은 `verified=ok`, 즉 코어가 참조 모델과 같은 결과를 낸 실행이다.

*This topic accumulates the core's throughput as measured by the benchmark harness `rex86_bench`, naming the host and the time. A figure means something **on that host alone**; comparing hosts means re-measuring the same workloads there. Every figure below comes from a `verified=ok` run, one in which the core produced the reference model's result.*

## 1. 방법 / Method

* 워크로드 여섯(`alu`, `memory`, `call`, `string`, `x87`, `mixed`), 프레임 예산 100만 명령, 20프레임, 기본 설정. 한 바퀴(lap)의 명령 수: alu 36,874, memory 67,083, call 38,921, string 53,258, x87 7,183, mixed 203,307.
* MIPS는 20프레임의 retire 합을 프레임 시간 합으로 나눈 값이다. 프레임 시간에는 바퀴 끝의 검증과 리셋이 포함된다.
* `string`의 MIPS는 다른 워크로드와 비교하지 않는다. REP STOSD/MOVSD 하나가 64 dword를 처리하고 명령 하나로 센다.
* 실시간 비율 = MIPS / (기판 클럭 MHz × IPC). **IPC 1.0은 추정**이다(설계 결정 4). 기판 CPU의 실제 지속 IPC는 측정되지 않았고, 1.0은 기준 CPU에 유리한 가정이다.

*Six workloads, a budget of one million instructions a frame, 20 frames, the defaults; a lap is 36,874 (alu), 67,083 (memory), 38,921 (call), 53,258 (string), 7,183 (x87) and 203,307 (mixed) instructions. MIPS is the 20 frames' retired sum over their time sum, verification and reset at lap ends included. `string`'s MIPS does not compare with the others (one REP moves 64 dwords and counts once). The real-time ratio is MIPS / (board clock MHz × IPC), and **IPC 1.0 is inferred** (design decision 4): the boards' sustained IPC is unmeasured and 1.0 favors the reference CPU.*

## 2. 기준선 (2026-10-09, #27, 커밋 d695b41 위의 작업 브랜치) / Baseline

**확인됨**. 호스트 둘, 같은 기계(AMD Ryzen 5 5600X, Zen 3, 6코어, 4.6 GHz 부스트). 다른 부하 없이 각각 한 번 실행했다.

| 호스트 | 빌드 |
|---|---|
| A: WSL2 Ubuntu 24.04, x86-64 | GCC 13.3, `linux-x64-release`(CMake Release 기본 `-O3 -DNDEBUG`) |
| B: Windows 11, x86(32비트) | MSVC 19.50 (Visual Studio 2022 Build Tools), `windows-x86-release` |

| 워크로드 | A MIPS | A p50 / p99 ms (100만 명령) | B MIPS | B p50 / p99 ms |
|---|---|---|---|---|
| alu | 3.82 | 255.6 / 348.9 | 2.36 | 420.7 / 446.2 |
| memory | 4.36 | 224.7 / 250.2 | 2.35 | 415.7 / 457.6 |
| call | 4.45 | 223.2 / 244.0 | 2.35 | 416.1 / 492.6 |
| string (REP 주의) | 2.57 | 383.3 / 429.6 | 1.82 | 547.9 / 569.4 |
| x87 | 3.72 | 262.7 / 307.2 | 2.04 | 486.4 / 509.2 |
| mixed | 3.57 | 284.8 / 292.0 | 2.23 | 448.3 / 456.3 |

첫 프레임까지의 시간은 모두 그 워크로드의 p50 프레임 시간과 같은 수준이다(A: 243~386 ms, B: 411~554 ms). 인터프리터는 워밍업이 없으므로 예상대로다. 메모리 계측: 게스트 버퍼 4,194,304바이트, 페이지 속성표 1,024바이트, `Cpu` 368바이트(A, 64비트 포인터) / 336바이트(B), `CpuState` 240바이트.

*Confirmed, two hosts on one machine (AMD Ryzen 5 5600X, Zen 3), each run once with nothing else running: A is WSL2 Ubuntu 24.04 x86-64 with GCC 13.3 Release, B is Windows 11 x86 (32-bit) with MSVC 19.50 Release. The table gives MIPS and the p50/p99 frame times of a million-instruction frame. Time to first frame equals a p50 frame everywhere (A 243-386 ms, B 411-554 ms), as expected of an interpreter without warm-up. Memory: a 4,194,304-byte guest buffer, a 1,024-byte page table, `Cpu` 368 bytes (A, 64-bit pointers) or 336 (B), `CpuState` 240 bytes.*

### 기판 기준 실시간 비율 / Real-time ratio by board (IPC 1.0, **추정**)

`mixed` 워크로드, 호스트 A:

| 기판 | 기준 CPU | 프레임 명령 수 | 프레임 시간(측정 MIPS 기준) | 실시간 비율 |
|---|---|---|---|---|
| ez2dj1 (K6-2 400 MHz) | 400 MHz | 6,666,667 | 1,866 ms | 0.0089 |
| mk3 (Mendocino Celeron 400 MHz) | 400 MHz | 6,666,667 | 1,866 ms | 0.0089 |
| mk5 (Tualatin Celeron 1.3 GHz) | 1,300 MHz | 21,666,667 | 6,065 ms | 0.0027 |
| ez2dj2 (Tualatin Celeron 1.4 GHz) | 1,400 MHz | 23,333,333 | 6,532 ms | 0.0026 |

목표는 16.7 ms, 비율 1.0이다. 인터프리터는 가장 느린 기판(400 MHz) 대비 **약 1/110**, 가장 빠른 기판(1.4 GHz) 대비 **약 1/390**이다. [#21 설계](../design/20261008-i021-target-board-cpu-baseline.md) 결정 4의 scratchpad 수치(Xeon 2.1 GHz, 약 3.5 MIPS)와 같은 자릿수이며, 이 표가 그 수치를 대체한다.

*For `mixed` on host A: ez2dj1 and mk3 (400 MHz) 6,666,667 instructions a frame, 1,866 ms at the measured MIPS, ratio 0.0089; mk5 (1.3 GHz) 21,666,667, 6,065 ms, 0.0027; ez2dj2 (1.4 GHz) 23,333,333, 6,532 ms, 0.0026. The target is 16.7 ms, ratio 1.0: the interpreter sits at about **1/110** of the slowest board and **1/390** of the fastest, the same order as the scratchpad figure in [#21 design](../design/20261008-i021-target-board-cpu-baseline.md) decision 4 (about 3.5 MIPS on a 2.1 GHz Xeon), which this table supersedes.*

## 2.1 디코드 캐시 뒤 (2026-10-09, #34) / After the decode cache

**확인됨**. 같은 기계, 같은 날, 다른 부하 없이 잰 전후다(전은 #34 직전의 main, 0.0.17). 디코드 캐시(직접 사상 4,096칸)와 묶음 인출이 들어갔다([#34 설계](../design/20261009-i034-interpreter-decode-cache.md)).

| 워크로드 | A 전 | A 후 | 배율 | B 전 | B 후 | 배율 |
|---|---|---|---|---|---|---|
| alu | 4.35 | 14.82 | 3.4 | 2.46 | 15.14 | 6.2 |
| memory | 4.54 | 16.65 | 3.7 | 2.48 | 16.36 | 6.6 |
| call | 4.39 | 16.88 | 3.8 | 2.47 | 18.09 | 7.3 |
| string (REP 주의) | 2.45 | 4.39 | 1.8 | 1.86 | 5.07 | 2.7 |
| x87 | 3.90 | 11.12 | 2.9 | 2.12 | 7.47 | 3.5 |
| mixed | 3.60 | 9.53 | 2.6 | 2.28 | 10.23 | 4.5 |

(A: WSL2 GCC 13 x86-64 Release, B: Windows MSVC 19.50 x86 Release, 단위 MIPS)

* mixed의 기판 환산(A, IPC 1.0 **추정**): 400 MHz 기판 실시간 비율 0.0089 → **0.024**(한 프레임 분량 699 ms), 1.4 GHz 기판 0.0026 → **0.0068**(2,447 ms). 실시간까지 아직 약 40배와 150배가 남는다.
* MSVC의 이득이 더 크다. 전에는 MSVC 빌드가 GCC보다 1.6~1.9배 느렸는데, 그 차이가 대부분 인출과 디코드에서 왔다는 뜻으로 **추정**한다(i386 ABI와 컴파일러의 차이가 그 경로에서 더 컸다).
* 메모리(목표 7): 디코드 캐시 4,751,360바이트(x86-64), 4,718,592바이트(x86). 페이지 속성표가 페이지마다 1바이트에서 5바이트(세대 4바이트 추가)가 됐다.
* 측정 중의 주의: 같은 기계에서 다른 무거운 실행과 겹치면 B가 mixed 5.23으로 반 가까이 떨어졌다. 수치는 반드시 단독으로 잰다(가이드).

**남은 병목**(gprof, A, `-O2 -pg`, 인라인 켬): `interp::Step` 자체 39%(정확한 폴트를 위한 레지스터, EFLAGS, 세그먼트 저장과 인라인된 `Execute`의 큰 분기), `GuestMemory::Write32` 10%와 `WriteVirtual`/`Linearize`(저장마다 범위, 페이지, `kTranslated` 검사), `RunUntilStop` 7%, 캐시 조회 약 8%(`Lookup`, `Generation`). 인출과 디코드는 프로파일에서 사라졌다. 다음 단계는 블록 단위 실행(분기 사이의 명령을 묶어 상태 저장과 루프 오버헤드를 줄임) 또는 3단계의 번역 백엔드다. 측정 없이 미리 고치지 않는다.

*Confirmed, before and after on the same machine and day with nothing else running (before: main just ahead of #34, 0.0.17), with the decode cache (4,096 direct-mapped slots) and bulk fetching. Mixed's board conversion (A, IPC 1.0 inferred): 0.0089 to **0.024** for the 400 MHz boards (699 ms a board frame), 0.0026 to **0.0068** for the 1.4 GHz board (2,447 ms), still about 40 and 150 times short of real time. MSVC gains more; its earlier 1.6-1.9x gap to GCC is inferred to have come mostly from fetching and decoding. Memory (goal 7): the decode cache holds 4,751,360 bytes (x86-64) or 4,718,592 (x86), and the page table grew from 1 to 5 bytes per page. Measuring alone matters: overlapping a heavy run on the same machine nearly halved B's mixed (5.23). Remaining bottlenecks: `interp::Step` itself 39% (saving registers, EFLAGS and segments for precise faults, and the inlined `Execute` switch), `GuestMemory::Write32` 10% with `WriteVirtual`/`Linearize` (range, page and `kTranslated` checks per store), `RunUntilStop` 7%, cache lookups about 8%; fetching and decoding are gone from the profile. Next would be block-level execution or phase 3's translation backends; nothing is fixed ahead of a measurement.*

## 2.2 블록 루프와 명령당 비용 정리 뒤 (2026-10-09, #35) / After the block loop and the per-instruction fixes

**확인됨**. 같은 기계에서 #34의 HEAD(전)와 #35(후)를 **번갈아** 세 번씩 잰 중앙값이다(기본 설정, 20프레임). 바뀐 것: 캐시 적중 경로의 디코드 버퍼 제거, 블록 루프, 게이트 필터, 결과 제자리 쓰기, `SlotOf` 표([#35 설계](../design/20261009-i035-block-execution.md)).

| 워크로드 | A 전 | A 후 | 배율 | B 전 | B 후 | 배율 |
|---|---|---|---|---|---|---|
| alu | 15.04 | 29.93 | 2.0 | 14.23 | 20.83 | 1.5 |
| memory | 16.09 | 31.68 | 2.0 | 15.38 | 21.38 | 1.4 |
| call | 16.35 | 30.88 | 1.9 | 13.44 | 23.52 | 1.8 |
| string (REP 주의) | 4.23 | 4.85 | 1.1 | 3.66 | 4.04 | 1.1 |
| x87 | 10.70 | 15.16 | 1.4 | 5.62 | 6.30 | 1.1 |
| mixed | 9.11 | 12.50 | 1.4 | 7.25 | 9.07 | 1.3 |

(A: WSL2 GCC 13 x86-64 Release, B: Windows MSVC x86 Release, 단위 MIPS)

* mixed의 기판 환산(A, IPC 1.0 **추정**): 400 MHz 기판 **0.031**(한 프레임 분량 533 ms), 1.4 GHz 기판 **0.0089**(1,867 ms).
* **#34 분석의 정정**: 2.1절의 "`interp::Step` 자체 39%(상태 저장과 분기)"는 대부분 GCC가 캐시 적중 때도 명령마다 1,144바이트 `std::optional<DecodedInstruction>` 지역 변수를 0으로 채운 비용이었다(표본 프로파일 31.9%, 디스어셈블로 `rep stos` 확인). gprof의 함수 단위 수치로는 나눌 수 없었다. 상태 저장 자체는 작다(세그먼트 복사를 줄여도 측정 차이 없음).
* B의 전 수치가 2.1절의 같은 코드(mixed 10.23)보다 낮다. 같은 바이너리를 오늘 잰 값이며 원인은 호스트 상태로 보이나 **미확정**이다. 그래서 전후를 번갈아 쟀고, 배율만 비교한다.
* A의 블록 루프 자체(검사를 블록마다로)는 측정 차이가 없었고, B에서는 5~10%였다. GCC가 명령마다의 루프 검사를 이미 싸게 만들었다는 뜻으로 **추정**한다.

**남은 병목**(표본 프로파일): A의 alu·memory·call에서 `ExecuteDecoded` 자신, `Execute`의 분기, 캐시 조회 약 11%(`Lookup`, 페이지 속성, 세대), `ReadOperand`/`WriteOperand`/`ReadGpr`(Zydis 피연산자 해석). B의 mixed에서 `WriteVirtual` 14.5%, `ExecuteStrings` 13.1%, `Write32` 6.7%, `PageAttributeTable::Get` 5.3%(저장마다 범위, 페이지, `kTranslated` 검사와 REP 문자열). 기본 블록 단위 조회는 얻을 몫이 조회 비용의 일부라 하지 않았다. 다음 후보는 저장 경로와 문자열 명령의 묶음 처리(#32와 겹침), 또는 3단계의 번역 백엔드다.

*Confirmed: medians of three **interleaved** runs each of #34's HEAD (before) and #35 (after) on the same machine, defaults, 20 frames, with the decode buffer gone from the cache-hit path, the block loop, the gate filter, results written in place and the `SlotOf` table. Mixed's board conversion (A, IPC 1.0 inferred): **0.031** for the 400 MHz boards (533 ms a board frame), **0.0089** for the 1.4 GHz board (1,867 ms). Correcting #34: section 2.1's "`interp::Step` itself 39% (state saving and dispatch)" was mostly GCC zeroing a 1,144-byte `std::optional<DecodedInstruction>` local on every instruction, cache hits included (31.9% in the sampling profile, `rep stos` in the disassembly), which gprof's per-function figure could not separate; state saving itself is small (cutting the segment copy made no measurable difference). B's before figures are lower than section 2.1's for the same code (mixed 10.23): the same binary measured today, the cause looking like host state but unresolved, which is why before and after were interleaved and only the ratios compare. The block loop by itself (checks per block) made no measurable difference on A and 5-10% on B, inferred to mean GCC already made the per-instruction loop checks cheap. Remaining bottlenecks (sampling profiles): on A's alu, memory and call, `ExecuteDecoded` itself, `Execute`'s dispatch, the cache lookup at about 11% (`Lookup`, page attributes, generations), and `ReadOperand`/`WriteOperand`/`ReadGpr` (Zydis operand resolution); on B's mixed, `WriteVirtual` 14.5%, `ExecuteStrings` 13.1%, `Write32` 6.7% and `PageAttributeTable::Get` 5.3% (range, page and `kTranslated` checks per store, and REP strings). Basic-block lookups were not done, their gain being part of the lookup's cost. Next candidates are the store path and batched string instructions (overlapping #32) or phase 3's translation backends.*

## 2.3 REP 반복을 단계로 센 뒤 (2026-10-10, #32) / After counting REP iterations as steps

근거: [#32 설계](../design/20261010-i032-rep-string-budget.md), [로그](../work-logs/20261010-i032-rep-string-budget.md)

**단위 변경(확인됨)**: #32부터 예산, `retired=`, `lap_instructions=`, `mips=`는 단계를 센다. REP 문자열은 반복 하나가 한 단계다. `string` 워크로드의 바퀴 단계 수는 53,258에서 569,354가 됐다. `mixed`는 `string`을 포함하므로 둘 다 이전 기록과 MIPS로 비교하지 않고, 바퀴당 비용으로 비교한다.

호스트 C: 클라우드 VM, Intel Xeon 2.80 GHz 4코어(모델명 `Intel(R) Xeon(R) Processor`), Ubuntu 24.04, GCC 13, `linux-x64-release`. 수정 전(`05f4f86`)과 #32를 번갈아 다섯 번씩 돌린 중앙값, 기본 설정(20프레임).

| 워크로드 | 수정 전 MIPS | #32 MIPS | 호스트 명령/단계 전 | 후 |
|---|---|---|---|---|
| alu | 17.31 | 16.81 | 503.6 | 505.8 |
| memory | 19.53 | 19.52 | 490.5 | 492.8 |
| call | 19.31 | 19.41 | 490.8 | 493.1 |
| x87 | 7.54 | 7.55 | 1046.3 | 1048.7 |
| string (단위 다름) | 3.63 | 41.34 | 바퀴당 152.3M | 바퀴당 163.7M |
| mixed (단위 다름) | 8.76 | 33.46 | | |

* **확인됨**: 호스트 C의 벽시계 수치는 같은 바이너리의 다섯 번 사이에서도 최소와 최대가 10% 넘게 벌어진다. 그래서 회귀 판단은 cachegrind의 호스트 명령 수(`Ir`)로 했다. 두 프레임과 여섯 프레임 실행의 차를 단계 수로 나눈 한계값이라 시작 비용이 빠진다.
* **확인됨**: REP가 아닌 워크로드의 비용은 단계당 호스트 명령 약 2개(0.4~0.5%) 늘었다. `ExecuteDecoded`가 `StepResult`의 예산 포인터를 `Ctx`로 옮기는 몫이다. 벽시계 중앙값의 차(alu −2.9%, 나머지 ±0.5%)는 잡음 범위 안이다.
* **확인됨**: `string`은 바퀴당 호스트 명령이 7.5% 늘었다. 반복마다 한도와 `attention`을 보는 몫이다. 같은 측정의 벽시계는 바퀴당 6.6% 빨랐으므로(초당 68.1 → 72.6바퀴) 이 차도 잡음 범위로 본다.
* **확인됨**: 같은 #32의 첫 두 구현은 단계당 호스트 명령이 16개 늘었다(alu 503.6 → 519.8). 명령마다 허용을 쓰고 문자열의 단계 보고를 읽는 몫, 그리고 일곱째 인자가 스택으로 넘어가며 레지스터를 밀어낸 몫이었다.

*Unit change (confirmed): from #32 on the budget, `retired=`, `lap_instructions=` and `mips=` count steps, one per REP string iteration; the `string` workload's lap went from 53,258 to 569,354 steps, and as `mixed` includes `string`, both compare with earlier records by cost per lap, not by MIPS. Host C: a cloud VM, Intel Xeon 2.80 GHz with 4 cores (model name `Intel(R) Xeon(R) Processor`), Ubuntu 24.04, GCC 13, `linux-x64-release`; medians of five interleaved runs each of the version before (`05f4f86`) and #32, defaults (20 frames), in the table above. Confirmed: host C's wall-clock figures spread by more than 10% between minimum and maximum across five runs of one binary, so the regression was judged by cachegrind's host instruction count (`Ir`), taken as the marginal value between runs of two and six frames divided by the steps, which leaves out the start-up cost. Confirmed: workloads without REP cost about 2 more host instructions per step (0.4-0.5%), `ExecuteDecoded` copying the budget pointer from the `StepResult` into `Ctx`; the wall-clock medians' differences (alu −2.9%, the rest within ±0.5%) are within the noise. Confirmed: `string` costs 7.5% more host instructions per lap, the per-iteration check of the limit and `attention`; its wall clock in the same measurement was 6.6% faster per lap (68.1 to 72.6 laps a second), so that difference too is taken as noise. Confirmed: #32's first two implementations cost 16 more host instructions per step (alu 503.6 to 519.8): writing the allowance per instruction and reading the string's step report, and a seventh argument going on the stack and pushing registers out.*

## 3. 읽는 법 / Reading

* **확인됨(#27 시점)**: 인터프리터는 명령 종류와 거의 무관하게 2~4.5 MIPS였다. `alu`(레지스터만)와 `memory`(적재와 저장)가 비슷하고, `call`도 비슷하다. 명령 하나의 비용이 의미 계산이 아니라 **디스패치(인출, 디코드, 피연산자 해석)에 지배된다**는 뜻이다. #21 설계의 프로파일(인출과 페이지 검사 40%, 디코드 27%)과 맞는다. 다음 단계(블록 캐시, 디코드 캐시, 번역 백엔드)의 근거였고, #34의 디코드 캐시가 이것을 확인했다(2.1절).
* **확인됨**: x86-64 GCC 빌드가 x86 MSVC 빌드보다 1.6~1.9배 빠르다. 호스트의 64비트 레지스터 수와 컴파일러 차이가 섞여 있어 원인은 나누지 않았다(미확정). 소비자의 Windows x86 oracle 용도에는 B가, 브라우저와 ARM의 참고에는 A가 가깝다.
* **확인됨**: p99 프레임 시간은 p50의 1.0~1.4배다. 인터프리터만 있는 지금 이 분포는 호스트 OS의 스케줄링 잡음이다. 번역 백엔드가 들어오면 워밍업과 번역 정지가 여기 더해진다(README 목표 8).
* **미확정**: wasm32(Node, 브라우저)와 AArch64 수치. 이 환경에 도구가 없어 CI 러너에서는 smoke만 돈다. 측정 뒤 이 문서에 절을 더한다.
* **추정**: IPC 1.0. 기판 실물이나 사이클 정확 자료가 생기면 `--ipc`로 다시 환산한다. 환산은 MIPS에 곱하는 것이므로 측정을 다시 할 필요는 없다.

*Confirmed: the interpreter runs at 2 to 4.5 MIPS almost regardless of instruction kind, `alu` (registers only), `memory` (loads and stores) and `call` alike, so an instruction's cost is **dominated by dispatch (fetch, decode, operand resolution), not semantics**, consistent with #21's profile (40% fetch and page checks, 27% decode) and grounding the next steps (block cache, decode cache, translation backends). Confirmed: the x86-64 GCC build is 1.6 to 1.9 times faster than the x86 MSVC build; the host's register count and the compiler differ at once, so the cause is not apportioned (unresolved); B is the closer reference for the consumers' Windows x86 oracle use, A for browser and ARM. Confirmed: p99 is 1.0 to 1.4 times p50, host scheduling noise with the interpreter alone; a translation backend's warm-up and stalls will add to it (README goal 8). Unresolved: wasm32 (Node, browser) and AArch64 figures, this environment lacking the tools and CI running smoke alone; a section follows once measured. Inferred: IPC 1.0, to be re-converted with `--ipc` when board hardware or cycle-accurate data exists; being a multiplication on MIPS it needs no re-measurement.*
