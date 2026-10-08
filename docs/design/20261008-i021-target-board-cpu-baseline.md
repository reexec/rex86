# #21 설계 : 최소 지원 사양을 대상 기판의 CPU로 재설정 / #21 design : resetting the minimum specification to the target boards' CPUs

이슈: [#21](https://github.com/reexec/rex86/issues/21) | 지시서: [20261008-i021](../work-orders/20261008-i021-target-board-cpu-baseline.md) | 로그: [20261008-i021](../work-logs/20261008-i021-target-board-cpu-baseline.md) | 근거: [대상 기판의 CPU](../kb/target-board-cpus.md)

## 배경 / Background

사용자가 최소 지원 사양을 정했다: rex86은 **안다미로 MK3, MK5 보드와 Amuse World EZ2DJ 1세대, 2세대 기판의 CPU**를 지원할 수 있어야 한다. 사양을 확인해 지금 목표와 다르면 재설정한다.

지금 목표는 두 곳에 있다. README 목표 1은 "IA-32 사용자 모드 정수 명령 전체와 x87, 그리고 **census가 확인하는** MMX/SSE"이고, 목표 3은 "원본 하드웨어(**Pentium II~III급**) 한 프레임 분량"이다. 헌장은 CPU 세대를 정하지 않는다. 구현은 386 스위트(SingleStepTests/80386)로 검증한 정수 명령과 P6까지의 x87이다.

*The user set the minimum specification: rex86 must support the CPUs of Andamiro's MK3 and MK5 boards and of Amuse World's generation 1 and 2 EZ2DJ boards, with the current targets reset where they differ. Those targets are README goal 1 ("the full IA-32 user-mode integer set, x87, and the MMX/SSE families **the census confirms**") and goal 3 ("one original-hardware frame (**Pentium II-III class**)"); the charter names no CPU generation; the implementation holds the integer set verified against the 386 suite and the x87 through P6.*

## 확인한 사양 / What was found

자세한 표와 출처는 [kb 문서](../kb/target-board-cpus.md)에 있다. 요약:

| 기판 | CPU | 명령 집합 |
|---|---|---|
| MK3 | Mendocino Celeron 333 ~ 400 MHz | P6 정수, P6 x87, MMX |
| MK5 | Celeron 1.0 또는 1.3 GHz (1.3 GHz는 Tualatin) | P6 정수, P6 x87, MMX, **SSE** |
| EZ2DJ 1세대 (1st ~ 6th TRAX) | AMD K6-2 300 MHz 이상 | Pentium급 정수, 387 x87, MMX, 3DNow! |
| EZ2DJ 2세대 (7th TRAX ~ EVOLVE) | Pentium III Coppermine 533 MHz ~ 1.0 GHz 또는 Tualatin Celeron 1.1 ~ 1.4 GHz | P6 정수, P6 x87, MMX, **SSE** |

기판 사양은 공동체 자료뿐이다. 처음 작성할 때는 원 문서 사이트가 막혀 검색 발췌로 확인했고, EZ2DJ 2세대는 미확정(Celeron 533 또는 K6-2)으로 두었다. 같은 날 후속 조사에서 원문(나무위키, Shiz arcade-docs의 GitHub 미러, Arcade Otaku와 EZ2 Wiki의 Wayback 사본)을 직접 읽어 위 표로 고쳤다. "Celeron 533"은 MAME가 적은 EZ2Dancer 2nd Move 기판이었고, EZ2DJ 2nd ~ 6th는 1세대 K6-2 기판의 게임이었다. 결론(명령 집합의 상한)은 바뀌지 않았다. 바뀐 것은 K6-2 기판의 게임 범위와 성능 기준의 상한이다(결정 2, 4).

*Details and sources are in the [kb document](../kb/target-board-cpus.md). The board specifications rest on community sources. When this design was first written the original sites were blocked, so they were read only as search excerpts and generation 2 EZ2DJ was left unresolved (a Celeron 533 or a K6-2). A follow-up the same day read the originals directly (NamuWiki, the GitHub mirror of Shiz's arcade-docs, Wayback copies of Arcade Otaku and EZ2 Wiki) and corrected the table above: the "Celeron 533" was MAME's EZ2Dancer 2nd Move board, and EZ2DJ 2nd through 6th were games of the generation 1 K6-2 board. The conclusion, the instruction-set ceiling, did not change; what changed is the range of games on the K6-2 board and the top of the performance reference (decisions 2 and 4).*

## 지금 목표와 다른 점 / How the current targets differ

| 항목 | 지금 | 대상 기판이 요구하는 것 |
|---|---|---|
| 정수 명령 | "IA-32 정수 전체", 실제 구현과 검증은 386 집합 | **P6 정수**: CMOVcc, CMPXCHG8B, BSWAP, XADD, CMPXCHG, UD2. 지금 인터프리터에 없음 |
| MMX | census가 확인하면 | **필수**(네 기판 모두) |
| SSE | census가 확인하면 | **필수**(MK5). Katmai SSE: XMM 산술, MMX 확장 정수 명령(PSHUFW 등), PREFETCH, SFENCE, MOVNTQ, MASKMOVQ, LDMXCSR/STMXCSR, FXSAVE/FXRSTOR |
| SSE2 이후 | 언급 없음 | **범위 밖**(어느 기판에도 없음) |
| 3DNow! | 언급 없음 | census 조건부(K6-2에만 있음) |
| x87 초월함수 | 2차 증분 예정 | 필수(모든 기판). 변화 없음 |
| EFLAGS | POPF가 AC와 ID를 쓰지 못함(386 동작) | AC와 ID 쓰기 가능(네 CPU 모두 486 이후, CPUID 판별에 ID 토글을 씀) |
| 성능 기준 | "Pentium II~III급" | 기판별 클럭표, 상한은 EZ2DJ 2세대의 1.4 GHz(Tualatin) |

## 결정 1: 최소 CPU 모델은 "P6 + MMX + SSE"(Pentium III급 사용자 모드) / Decision 1: the minimum CPU model is P6 + MMX + SSE

README 목표 1의 범위를 바꾼다: **IA-32 사용자 모드의 P6 정수 명령 전체, P6 x87 전체(초월함수 포함), MMX, Pentium III의 SSE(FXSAVE/FXRSTOR 포함).** SSE2 이후는 범위 밖이다. 3DNow!는 EZ2DJ 1세대 기판의 게임(1st ~ 6th TRAX)의 census가 사용을 확인할 때만 범위에 넣는다. census는 이제 범위를 정하는 수단이 아니라 범위를 넘는 것(3DNow!)을 찾는 수단이자 커버리지 측정 수단이다.

*README goal 1's scope becomes **the whole P6 integer set in IA-32 user mode, the whole P6 x87 (transcendentals included), MMX, and the Pentium III's SSE (FXSAVE/FXRSTOR included)**. SSE2 and later are out of scope; 3DNow! enters only if a census of the generation 1 EZ2DJ board's games (1st through 6th TRAX) confirms its use. The census stops being what sets the scope and becomes what finds anything beyond it (3DNow!) and what measures coverage.*

## 결정 2: 기판의 CPU는 기능 플래그로 흉내 낸다 / Decision 2: a board's CPU is emulated through the feature flags

코어는 상한(P6 + MMX + SSE)을 구현하고, 소비자는 흉내 낼 기판에 맞춰 `Features`를 끈다. 꺼진 기능의 명령은 그 CPU처럼 #UD다. 지금 `Features`에는 `x87`, `mmx`, `sse`, `sse2`, `segments_16bit`가 있다. K6-2를 흉내 내려면 CMOVcc와 FCOMI/FCMOVcc를 끌 수 있어야 하므로, CPUID의 CMOV 비트에 대응하는 플래그를 더한다(CPUID.01H:EDX.CMOV는 CMOVcc와, FPU가 있으면 FCOMI/FCMOVcc의 존재를 뜻한다). 이 공개 계약 변경은 CMOV를 구현하는 #22에서 한다.

| 기판 | x87 | P6 추가(CMOV, FCOMI, FCMOV) | MMX | SSE |
|---|---|---|---|---|
| MK3 | 켬 | 켬 | 켬 | 끔 |
| MK5 | 켬 | 켬 | 켬 | 켬 |
| EZ2DJ 1세대 | 켬 | **끔** | 켬 | 끔 |
| EZ2DJ 2세대 | 켬 | 켬 | 켬 | 켬 |

EZ2DJ 2세대의 두 CPU(Coppermine Pentium III, Tualatin Celeron)는 모두 SSE가 있고 SSE2가 없다. 그래서 변형과 무관하게 플래그가 같다. EZ2DJ 2nd ~ 6th TRAX는 1세대 줄(K6-2)을 따른다.

*Both generation 2 CPUs (the Coppermine Pentium III and the Tualatin Celeron) have SSE and lack SSE2, so the flags are the same whichever variant a board carries. EZ2DJ 2nd through 6th TRAX follow the generation 1 row (K6-2).*

CPUID와 RDTSC는 지금처럼 `Environment`가 답한다. 게임이 CPUID로 기능을 확인하므로 소비자의 CPUID 응답과 `Features`가 같은 CPU를 말해야 한다. 네 기판의 CPUID 참조 값을 이 저장소가 데이터로 제공할지는 소비자 통합(2A/2B) 때 정한다.

*The core implements the ceiling and a consumer turns `Features` off to match the board it emulates, a disabled feature's instructions raising #UD as on that CPU. Emulating a K6-2 needs CMOVcc and FCOMI/FCMOVcc switchable, so a flag matching CPUID's CMOV bit is added (CPUID.01H:EDX.CMOV means CMOVcc and, with an FPU, FCOMI/FCMOVcc); that contract change is made in #22, which implements CMOV. CPUID and RDTSC stay answered by `Environment`; games check features through CPUID, so a consumer's CPUID answers and its `Features` must describe the same CPU. Whether this repository ships reference CPUID values for the four boards is decided at consumer integration (2A/2B).*

## 결정 3: EFLAGS는 486 이후의 동작 / Decision 3: EFLAGS behave as on the 486 and later

네 CPU 모두 EFLAGS의 AC(비트 18)와 ID(비트 21)를 쓸 수 있다. 게스트의 CPU 판별 코드는 AC 토글로 486을, ID 토글로 CPUID 지원을 확인한다. 지금 POPF와 IRET의 쓰기 마스크(`0x7FD5`)는 386처럼 둘을 버리므로 게스트가 자신을 386으로 판별한다. 둘을 쓰기 가능으로 바꾼다. 정렬 검사 예외(#AC)는 모델링하지 않는다: AC가 효과를 내려면 CR0.AM과 CPL 3이 필요하고, 사용자 모드 코어의 게스트(DOS/4GW, Windows 98)는 정렬 검사를 쓰지 않는다. 이 변경은 정수 명령을 다루는 #22에서 한다.

*All four CPUs let EFLAGS.AC (bit 18) and ID (bit 21) be written; guest CPU detection toggles AC to find a 486 and ID to find CPUID. Today's POPF/IRET write mask (`0x7FD5`) drops both as a 386 does, so guests detect a 386; both become writable. The alignment-check exception (#AC) is not modeled: AC takes effect only with CR0.AM at CPL 3, and the user-mode core's guests (DOS/4GW, Windows 98) do not use alignment checking. The change is made in #22, which works on the integer set.*

## 결정 4: 성능 목표는 기판별 클럭을 기준으로 한다 / Decision 4: the performance target is set by each board's clock

README 목표 3의 "Pentium II~III급"을 기판별 기준으로 바꾼다. 실시간 판정은 **그 게임이 돌던 기판**을 기준으로 하고, 한 기판에 CPU 변형이 여럿이면 **가장 빠른 변형**을 기준으로 한다. 같은 게임이 모든 변형에서 돌았으므로 가장 빠른 변형에서의 동작까지 재현해야 안전하다. 그래서 네 기판 중 가장 빠른 EZ2DJ 2세대(Tualatin Celeron 1.4 GHz)의 게임도 실시간이어야 한다. 이 상한은 처음 작성 때의 MK5(1.3 GHz)에서 후속 조사 뒤 사용자 결정으로 바뀌었다.

| 기판 | 기준 CPU(가장 빠른 변형) | 이 기판의 게임 |
|---|---|---|
| EZ2DJ 1세대 | K6-2 400 MHz | EZ2DJ 1st ~ 6th TRAX |
| MK3 | Mendocino Celeron 400 MHz | rePIU의 DOS/4GW 게임 |
| MK5 | Tualatin Celeron 1.3 GHz | Premiere 3, PREX 3의 MK5판, Exceed |
| EZ2DJ 2세대 | Tualatin Celeron 1.4 GHz | EZ2DJ 7th TRAX ~ EZ2AC EVOLVE |

수치(MIPS, 프레임 시간)는 벤치마크 하네스가 생긴 뒤 이 표에 맞춰 확정한다. 참고로 지금 인터프리터는 Release 빌드에서 명령 종류와 무관하게 약 3.5 MIPS다(Intel Xeon 2.1 GHz, 합성 루프 넷, scratchpad 측정. 시간의 40%가 바이트 단위 인출과 페이지 검사, 27%가 디코드). 333 MHz P6의 처리량과 두 자릿수 차이가 나므로, 이 목표는 번역 백엔드(3단계)와 인터프리터 빠른 경로를 전제로 한다. JIT가 금지된 호스트(iOS)에서 인터프리터만으로 어느 기판까지 실시간인지는 측정으로 정하고, 이 절에 적는다.

*Goal 3's "Pentium II-III class" becomes a per-board reference: real time is judged against **the board the game ran on**, and where a board shipped with several CPU variants, against **the fastest variant**: the same game ran on every variant, so reproducing its behavior up to the fastest one is the safe bar. The games of the fastest board, generation 2 EZ2DJ (Tualatin Celeron 1.4 GHz), must therefore run in real time too; this top moved from the MK5 (1.3 GHz) of the first draft by the user's decision after the follow-up research. The reference CPUs: EZ2DJ generation 1, K6-2 400 MHz (EZ2DJ 1st through 6th TRAX); MK3, Mendocino Celeron 400 MHz (rePIU's DOS/4GW games); MK5, Tualatin Celeron 1.3 GHz (Premiere 3, the MK5 builds of PREX 3, Exceed); EZ2DJ generation 2, Tualatin Celeron 1.4 GHz (EZ2DJ 7th TRAX through EZ2AC EVOLVE). Numbers (MIPS, frame times) are fixed against this table once the benchmark harness exists. For reference, today's interpreter runs at about 3.5 MIPS in Release regardless of the instruction mix (Intel Xeon 2.1 GHz, four synthetic loops, measured in a scratchpad; 40% of the time goes to byte-wise fetching with page checks, 27% to decoding), two orders of magnitude below a 333 MHz P6, so the target presupposes the translation backends (phase 3) and an interpreter fast path. How far the interpreter alone reaches real time on JIT-forbidden hosts (iOS) is measured and recorded in this section.*

## 결정 5: 단계와 문서 / Decision 5: phases and documents

* README 목표 1과 3, 달성도 표, 헌장, ARCHITECTURE를 결정 1~4대로 고친다.
* 달성도 표에 두 줄을 더한다: **P6 정수 보완**(1단계, #22에서 정수 명령 호스트 대조 fuzz와 함께), **MMX와 SSE**(새 단계 1b, 미착수).
* x87 초월함수는 원래 계획대로 1단계에 남는다.
* 코드는 바꾸지 않는다. 결정 2와 3의 구현은 #22, MMX와 SSE는 별도 작업이다.

*README goals 1 and 3, the attainment table, the charter and ARCHITECTURE change per decisions 1-4; the attainment table gains **P6 integer completion** (phase 1, in #22 with the integer host-comparison fuzz) and **MMX and SSE** (new phase 1b, not started); the x87 transcendentals stay in phase 1 as planned. No code changes: decisions 2 and 3 are implemented in #22, MMX and SSE in a task of their own.*

## 소비자 영향 / Consumer impact

이 작업은 공개 계약을 바꾸지 않는다. #22가 `Features`에 CMOV 플래그를 더하고 POPF의 AC/ID 동작을 바꾸면, 두 소비자는 흉내 낼 기판에 맞춰 `Features`와 CPUID 응답을 정해야 한다(결정 2의 표).

*No contract change here. Once #22 adds the CMOV flag to `Features` and changes POPF's AC/ID behavior, both consumers set `Features` and their CPUID answers for the board they emulate (decision 2's table).*

## 검증 / Verification

문서 작업이다. 바뀐 문서의 링크가 실제 파일을 가리키는지, 결정과 README, 헌장의 서술이 서로 맞는지 확인한다. 빌드는 바뀌지 않으므로 push 뒤 CI가 그대로 녹색인지만 본다.

*A documentation task: check that the changed documents' links resolve and that the decisions, README and charter agree; the build is untouched, so CI need only stay green after the push.*
