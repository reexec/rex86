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

## 3. 읽는 법 / Reading

* **확인됨(#27 시점)**: 인터프리터는 명령 종류와 거의 무관하게 2~4.5 MIPS였다. `alu`(레지스터만)와 `memory`(적재와 저장)가 비슷하고, `call`도 비슷하다. 명령 하나의 비용이 의미 계산이 아니라 **디스패치(인출, 디코드, 피연산자 해석)에 지배된다**는 뜻이다. #21 설계의 프로파일(인출과 페이지 검사 40%, 디코드 27%)과 맞는다. 다음 단계(블록 캐시, 디코드 캐시, 번역 백엔드)의 근거였고, #34의 디코드 캐시가 이것을 확인했다(2.1절).
* **확인됨**: x86-64 GCC 빌드가 x86 MSVC 빌드보다 1.6~1.9배 빠르다. 호스트의 64비트 레지스터 수와 컴파일러 차이가 섞여 있어 원인은 나누지 않았다(미확정). 소비자의 Windows x86 oracle 용도에는 B가, 브라우저와 ARM의 참고에는 A가 가깝다.
* **확인됨**: p99 프레임 시간은 p50의 1.0~1.4배다. 인터프리터만 있는 지금 이 분포는 호스트 OS의 스케줄링 잡음이다. 번역 백엔드가 들어오면 워밍업과 번역 정지가 여기 더해진다(README 목표 8).
* **미확정**: wasm32(Node, 브라우저)와 AArch64 수치. 이 환경에 도구가 없어 CI 러너에서는 smoke만 돈다. 측정 뒤 이 문서에 절을 더한다.
* **추정**: IPC 1.0. 기판 실물이나 사이클 정확 자료가 생기면 `--ipc`로 다시 환산한다. 환산은 MIPS에 곱하는 것이므로 측정을 다시 할 필요는 없다.

*Confirmed: the interpreter runs at 2 to 4.5 MIPS almost regardless of instruction kind, `alu` (registers only), `memory` (loads and stores) and `call` alike, so an instruction's cost is **dominated by dispatch (fetch, decode, operand resolution), not semantics**, consistent with #21's profile (40% fetch and page checks, 27% decode) and grounding the next steps (block cache, decode cache, translation backends). Confirmed: the x86-64 GCC build is 1.6 to 1.9 times faster than the x86 MSVC build; the host's register count and the compiler differ at once, so the cause is not apportioned (unresolved); B is the closer reference for the consumers' Windows x86 oracle use, A for browser and ARM. Confirmed: p99 is 1.0 to 1.4 times p50, host scheduling noise with the interpreter alone; a translation backend's warm-up and stalls will add to it (README goal 8). Unresolved: wasm32 (Node, browser) and AArch64 figures, this environment lacking the tools and CI running smoke alone; a section follows once measured. Inferred: IPC 1.0, to be re-converted with `--ipc` when board hardware or cycle-accurate data exists; being a multiplication on MIPS it needs no re-measurement.*
