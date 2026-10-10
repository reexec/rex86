# 32비트 정수 명령 호스트 CPU 대조에서 확인된 사실 / Facts from the 32-bit integer host-CPU comparison

근거 작업: [#22](https://github.com/reexec/rex86/issues/22) ([설계](../design/20261008-i022-integer-host-fuzz-and-trace.md), [로그](../work-logs/20261008-i022-integer-host-fuzz-and-trace.md)) | 도구: `rex86_int_fuzz`([가이드](../guides/integer-host-fuzz-and-traces.md)) | 기법: [트랩 플래그 단일 스텝](../kb/trap-flag-single-step.md) | 측정 호스트: Intel Xeon Cascade Lake(family 6 model 85 stepping 7, 긴 실행), AMD EPYC 7763(Zen 3, CI `linux-x86` 작업의 짧은 실행), 둘 다 Linux x86-64 커널 위의 i386 프로세스

상태 표기: **확인됨**(호스트에서 같은 바이트를 실행해 일치를 확인), **추정**, **미확정**. "확인됨"은 위 Intel 호스트의 긴 실행에서의 확인이고, AMD에서는 CI의 5만 건에서 어긋나지 않았다는 데까지다. 386 세대와 다른 점은 SingleStepTests/80386(386EX, [분석](singlesteptests-386ex-deviations.md))과 함께 적는다.

*Status marks as elsewhere; "confirmed" means confirmed by the long runs on the Intel host above, AMD's share being that the CI's 50,000 cases did not disagree. Where the 386 generation differs, SingleStepTests/80386 (a 386EX, [analysis](singlesteptests-386ex-deviations.md)) is cited alongside.*

## 1. 방법 / Method

i386 프로세스가 0x10000에 코드 페이지, 8 KiB 작업 영역, 8 KiB 보호 영역을 고정 매핑하고, 무작위 정수 명령 하나를 `popfd`(TF=1) 바로 뒤에서 실행한다. 단일 스텝 트랩이나 폴트 신호의 컨텍스트가 결과다. 코어는 **같은 게스트 주소**에 같은 내용을 두고 trace 한 건으로 재생한다(`trace::Replay`). 결과 종류(retire, 폴트 종류), GPR 8개, EIP, EFLAGS(RF 제외), 코드 페이지와 작업 영역 전체, #PF의 폴트 주소를 비교한다.

생성: 1바이트와 `0F xx` opcode의 modrm.reg별 형태 1,112개(149개 mnemonic, ISA 집합 I86, I186, I386, I486, I486REAL, PENTIUMREAL, PPRO, CMOV, LAHF, FAT_NOP, PAUSE). 66, 67, 세그먼트 override, F2/F3, F0 prefix를 확률로 붙이고, 메모리 피연산자(숨은 [ESI], [EDI], [ESP] 포함)를 작업 영역으로 옮긴다. 환경과 권한에 묶인 명령은 뺐다(설계 결정 3).

결과(최종 바이너리, Intel, 시드 100~109 × 100만): 1,000만 건 **불일치 0**. 약 93.6%가 retire, 나머지는 폴트(#PF, #GP, #UD, #DE, #BR 순)다. CI의 AMD EPYC 7763에서도 ctest의 5만 건(시드 1)이 불일치 0이다.

*An i386 process maps a code page, an 8 KiB work area and an 8 KiB guard at 0x10000 and runs one random integer instruction right after a `popfd` that sets TF; the single-step trap's or the fault signal's context is the outcome. The core holds the same contents at **the same guest addresses** and replays the case as one trace (`trace::Replay`), comparing the outcome (retired, fault kind), the eight GPRs, EIP, EFLAGS (RF aside), the whole code page and work area, and #PF's fault address. Generation draws from 1,112 forms (149 mnemonics) over one-byte and `0F xx` opcodes per modrm.reg, adds 66, 67, segment-override, F2/F3 and F0 prefixes by chance, and moves every memory operand (the hidden [ESI], [EDI], [ESP] included) into the work area; environment- and privilege-bound instructions are left out (design decision 3). Final binary on Intel, seeds 100-109 × 1M: 10M cases with **zero mismatches**, about 93.6% retiring and the rest faulting (#PF, #GP, #UD, #DE, #BR in that order); ctest's 50,000 cases (seed 1) show zero mismatches on the CI's AMD EPYC 7763 too.*

## 2. 코어에 반영한 동작 / Behavior adopted by the core

처음 2만 건에서 21건, 이후 긴 실행에서 몇 건이 나왔고, 모두 아래로 고쳤다. SDM이 분명한 곳은 SDM대로, 침묵하는 곳은 실측값으로 정했다. SST(386EX) 실행 비교는 바뀌지 않았다(1,741,900건 불일치 0).

| 항목 | 확인된 동작 | 근거 |
|---|---|---|
| 읽기 전용 세그먼트에 쓰기 | CS override로 쓰면 평탄 세그먼트여도 #GP. 읽고 쓰는 목적지(ADD [m], r, BTS [m], r, CMPXCHG8B 등)는 **읽기 전에** 쓰기 검사가 먼저라, 페이지 폴트가 날 주소여도 #GP가 이긴다 | 확인됨. SDM Vol. 3 5.5(코드 세그먼트 쓰기 금지) |
| 페이지 경계를 넘는 접근의 폴트 주소 | 처음 닿지 못하는 바이트(CR2). dword가 0x12FFE에서 비매핑 0x13000으로 넘어가면 0x13000 | 확인됨. 코어의 `Event::fault_address`가 이 값을 보고한다 |
| ENTER의 최종 ESP 검사 | 프레임 push가 메모리에 남은 뒤, 최종 ESP(ESP − 할당 크기)에 쓰기를 시험해 #PF를 낸다. 폴트 주소는 최종 ESP | 확인됨. SDM ENTER의 #PF 조건("a write using the final value of the stack pointer") |
| ENTER의 display 복사 | (E)BP를 **스택 크기**(SS.B)로 줄여 가며 읽는다. 66 prefix의 16비트 ENTER도 32비트 스택에서는 EBP 전체를 쓴다 | 확인됨. SDM ENTER 의사 코드("IF StackSize = 32 … EBP ← EBP − 2") |
| CMPS의 읽기 순서 | ES:(E)DI를 먼저 읽는다. 양쪽이 모두 폴트 나는 주소면 목적지 쪽이 보고된다 | 확인됨(Intel Cascade Lake, AMD Zen 3). SDM은 순서를 적지 않는다. 386EX SST와도 충돌 없음. AMD Zen 5는 원본 쪽을 보고한다(4절, 제조사 이탈) |
| BOUND의 검사 순서 | 두 경계의 세그먼트 limit을 먼저 검사하고(386EX SST), 하한을 읽어 비교한 다음에 상한을 읽는다. 첨자가 하한보다 작으면 상한이 폴트 날 페이지에 있어도 #BR | 확인됨(Intel)과 SST(386EX)를 함께 만족하는 순서. AMD Zen 3은 두 경계를 먼저 읽는다(4절 앞, 제조사 이탈) |
| XADD [m], r의 주소 | 원본 레지스터가 주소의 일부여도(XADD [ecx], ch) 주소는 한 번만 계산된다 | 확인됨. 코어의 구현 오류였다(레지스터를 먼저 써서 주소가 바뀌었음) |
| PAUSE(F3 90) | NOP | 확인됨. Pentium 4 이전에는 REP NOP |

*Fixed after 21 mismatches in the first 20,000 cases and a few more in long runs; SDM-clear cases follow the SDM, silent ones the measurement, and the SST (386EX) comparison stays at 1,741,900 executed with zero mismatches. A write through a CS override is #GP even on a flat segment, and a read-modify-write destination is checked for writing **before the read**, so #GP beats a page fault. A page-straddling access reports the first unreachable byte (CR2), as `Event::fault_address` now does. ENTER probes a write at the final ESP after the frame pushes land, faulting there (the SDM's "write using the final value of the stack pointer"), and walks the display at the **stack** width, so a 16-bit ENTER on a 32-bit stack steps all of EBP (the SDM pseudocode). CMPS reads ES:(E)DI first, so the destination is reported when both sides fault (the SDM gives no order). BOUND checks both bounds against the segment limit first (386EX), then compares the lower bound before reading the upper, raising #BR even when the upper's page would fault (Intel). XADD [m], r computes its address once even when the source register is part of it, which the core got wrong. PAUSE is a NOP.*

## 3. 미정의로 마스크한 것 / Masked as undefined

SDM이 미정의로 두는 것과 호스트의 인공물은 비교에서 뺀다(설계 결정 4): Zydis가 명령별로 알려 주는 미정의 플래그, 시프트와 회전의 count ≠ 1인 OF와 count ≥ 폭인 SHL/SHR/SAR의 CF, 폭을 넘는 count의 SHLD/SHRD 결과와 플래그, 원본이 0인 BSF/BSR의 목적지, 16비트 BSWAP의 목적지, 32비트 PUSH sreg의 상위 2바이트, EFLAGS.RF, POPF의 IF와 IOPL.

여기에 하나를 더했다.

* **확인됨(세대 차이): 반복 도중 폴트 난 REP CMPS/SCAS의 플래그.** 반복 몇 번을 마친 뒤 폴트가 나면 ECX, ESI, EDI는 양쪽 모두 완료된 반복까지 진행해 있다. EFLAGS는 Intel Cascade Lake에서 **명령 시작 때의 값**이고, 386EX(SST)에서는 **마지막으로 끝난 반복의 비교 결과**다. SDM은 이 경우의 플래그를 적지 않고, 다시 실행하면 첫 반복이 플래그를 새로 계산하므로 보통의 게스트는 차이를 볼 수 없다. 코어는 SST로 검증된 386EX 쪽을 유지하고, fuzz는 반복이 진행된 폴트에서 산술 플래그를 마스크한다.

*What the SDM leaves undefined and the host's artifacts are masked (design decision 4, listed above), plus one more: **confirmed, a generation difference: the flags of a REP CMPS/SCAS faulting mid-way.** After some iterations, ECX, ESI and EDI have advanced through the completed ones on both sides, but EFLAGS reads **as the instruction started** on the Intel Cascade Lake and **as the last completed iteration left it** on the 386EX (SST). The SDM does not say, and a re-executed instruction recomputes the flags in its first iteration, so ordinary guests cannot tell. The core keeps the SST-verified 386EX behavior and the fuzz masks the arithmetic flags of such faults.*

### 제조사 이탈: Zen 5의 CMPS 읽기 순서 / Vendor deviation: Zen 5's CMPS read order

* **확인됨(CI 관측, 2026-10-09, #27 PR의 CI): CMPS의 두 피연산자가 모두 닿을 수 없을 때 AMD EPYC 9V45(Zen 5)는 원본 DS:(E)SI 쪽 주소를 보고한다.** 시드 1의 5만 건에서 CMPSB/W/D 4건이 폴트 주소만 달랐다. 같은 시드가 AMD Ryzen 5 5600X(Zen 3)의 i386 프로세스에서는 불일치 0이므로, Zen 3과 Intel Cascade Lake는 목적지 쪽을 보고한다. SDM은 순서를 적지 않는다.
* 코어는 목적지 쪽을 유지한다(Intel, Zen 3, 386EX SST와 같다). fuzz는 이 경우를 `vendor_deviations`로 따로 세고 trace에 기록하지 않는다. 판정 조건은 좁다: 폴트 주소만 빼면 전부 일치하고, 호스트의 CR2가 원본 피연산자 범위 안이면서 매핑 밖이고, 코어의 주소가 목적지 피연산자 범위 안일 때만이다. 코어가 다른 주소를 내면 여전히 불일치다.
* **추정**: Zen 5가 원본을 먼저 읽는 것이 모든 CMPS 형태(REP, 16비트 주소)에 같다는 것. 관측 4건이 판정 조건을 만족하는지는 다음 Zen 5 CI 실행에서 `vendor_deviations`로 확인한다. 이 기계에서는 호스트 결과를 원본 주소로 바꾼 시뮬레이션으로 판정 경로를 시험했다(작업 로그).
* **확인됨(CI 관측, 2026-10-10, [#40](https://github.com/reexec/rex86/issues/40)): 원본이 주소조차 만들 수 없을 때도 Zen 5는 원본을 먼저 본다.** AMD EPYC 9V45에서 `64 67 A6`(FS 접두어, 16비트 주소 CMPSB)의 원본 FS:SI는 null FS 선택자로 #GP, 목적지 ES:DI(0xFFEF)는 매핑 밖으로 #PF인 사례가 있었다. Zen 5는 #GP를 냈고, 코어와 Zen 3은 목적지의 #PF를 냈다(같은 시드 30000001이 Ryzen 5 5600X에서 불일치 0, 제조사 차이 0). 같은 2만 건 네 묶음에서 Zen 5의 `vendor_deviations`는 5, 3, 0, 3, Zen 3은 모두 0이었다.
* fuzz는 이 경우도 `vendor_deviations`로 센다. 조건: CMPS이고, 호스트가 #GP이고, 원본 세그먼트가 null 선택자이고(이 fuzz의 세그먼트는 평탄해서 원본의 #GP는 이 길뿐이다), 목적지가 매핑 밖이며, 호스트의 기대값을 목적지 바이트의 #PF로 바꾸면 나머지가 모두 일치할 때만이다.
* 이 관측으로 위 **추정**에 16비트 주소와 세그먼트 접두어의 경우가 더해졌다. REP 형태는 아직 따로 관측하지 않았다.

*Confirmed (CI observation, 2026-10-09, on #27's PR): with both CMPS operands unreachable, the AMD EPYC 9V45 (Zen 5) reports the source DS:(E)SI address. Seed 1's 50,000 cases gave 4 CMPSB/W/D cases differing in the fault address alone; the same seed has zero mismatches in an i386 process on an AMD Ryzen 5 5600X (Zen 3), so Zen 3 and the Intel Cascade Lake report the destination. The SDM does not say. The core keeps the destination (as Intel, Zen 3 and the 386EX SST), and the fuzz counts the case under `vendor_deviations` and keeps it out of the corpus, under narrow conditions: everything but the fault address matches, the host's CR2 lies in the source operand and outside the mapping, and the core's address lies in the destination operand; any other core address stays a mismatch. Inferred: that Zen 5 reads the source first in every CMPS form (REP, 16-bit addressing); the next Zen 5 CI run confirms through `vendor_deviations` that the observed cases meet the conditions. Here, the judgment was exercised by a simulation that rewrote the host's result to the source address (work log). Confirmed (CI observation, 2026-10-10, #40): Zen 5 looks at the source first even when the source cannot be addressed. On an AMD EPYC 9V45, `64 67 A6` (FS prefix, 16-bit addressing CMPSB) had its source FS:SI raise #GP through the null FS selector while its destination ES:DI (0xFFEF) lay outside the mapping; Zen 5 raised #GP, the core and Zen 3 the destination's #PF (the same seed 30000001 gives zero mismatches and zero deviations on the Ryzen 5 5600X); over the same four batches of 20,000 cases Zen 5 counted 5, 3, 0 and 3 vendor deviations and Zen 3 none. The fuzz counts this case under `vendor_deviations` too, only when it is a CMPS, the host raised #GP, the source segment is a null selector (this fuzz's segments are flat, so that is the source's only way to #GP), the destination lies outside the mapping, and everything else matches once the host's expectation reads as a #PF at a destination byte. This adds 16-bit addressing and segment prefixes to the estimate above; the REP forms have not been observed separately yet.*

### 제조사 이탈: Zen 3의 BOUND 읽기 순서 / Vendor deviation: Zen 3's BOUND read order

* **확인됨(2026-10-09, AMD Ryzen 5 5600X, Zen 3, i386 프로세스): BOUND는 비교 전에 두 경계를 모두 읽는다.** 첨자가 하한보다 작고 상한이 닿을 수 없는 페이지(가드 0x13000)에 있으면, Intel Cascade Lake와 코어는 #BR을, Zen 3은 상한 주소의 페이지 폴트를 낸다. 250만 건(시드 1, 101~104 × 50만)에서 32건이다. 지금까지 AMD 긴 실행이 없어 드러나지 않았다.
* 코어는 Intel의 순서를 유지한다(SST 386EX와도 맞는다). fuzz는 코어가 #BR을 내고, 호스트의 CR2가 상한 피연산자 안이면서 매핑 밖이고, 호스트의 폴트를 #BR로 읽으면 나머지가 모두 일치할 때만 `vendor_deviations`로 센다.
* 같은 실행에서 CMPS 이탈은 0건이었다. 그러므로 Zen 3은 CMPS에서 Intel과 같고, BOUND에서 다르다.

*Confirmed (2026-10-09, AMD Ryzen 5 5600X, Zen 3, i386 process): BOUND reads both bounds before comparing. With the index below the lower bound and the upper bound on an unreachable page (the guard at 0x13000), the Intel Cascade Lake and the core raise #BR while Zen 3 page-faults at the upper bound; 32 cases in 2.5M (seeds 1 and 101-104 × 500,000), unseen before for lack of a long AMD run. The core keeps Intel's order (consistent with the 386EX SST); the fuzz counts the case under `vendor_deviations` only when the core raises #BR, the host's CR2 lies in the upper bound and outside the mapping, and the case matches once the host's fault reads as #BR. The same runs had no CMPS deviation: Zen 3 agrees with Intel on CMPS and differs on BOUND.*

## 4. 미확정 / Unresolved

* AMD Zen 5의 긴 실행(CI는 5만 건뿐)과 P6 세대 실물에서의 위 모든 항목. Zen 3은 2026-10-09에 250만 건을 돌렸다. 확인 방법: 그 호스트의 i386 프로세스에서 `rex86_int_fuzz`를 긴 시드로 실행한다(가이드).
* 문자열이 아닌 명령에 붙은 F2/F3: SDM은 이 사용을 예약으로 두고 "예측할 수 없는 동작"을 허용한다(Vol. 2, 2.1.1). 실제로 F3 LOOPNE의 분기 여부가 AMD EPYC 7763(CI, 7건)과 Intel 호스트, 코어 사이에서 갈렸다. 생성기는 이 조합을 만들지 않는다(PAUSE, F3 90만 예외).
* 환경과 권한에 묶여 뺀 명령(세그먼트 적재, far 제어 흐름, IRET, 포트 I/O, INT n, 특권 명령): 보호 모드 의미가 호스트 OS의 GDT/LDT에 묶인다. 실모드 의미는 SST가 검증한다.
* 16비트 주소(67 prefix)의 메모리 접근은 호스트에서 늘 0x10000 아래의 비매핑 주소라 폴트 경로만 비교된다. 16비트 주소의 성공 경로는 SST가 검증한다.

*Unresolved: everything above in long runs on AMD hosts and on P6-generation hardware (run `rex86_int_fuzz` with long seeds in an i386 process there, per the guide); F2/F3 on non-string instructions, which the SDM reserves with "unpredictable behavior" (Vol. 2, 2.1.1) and where F3 LOOPNE indeed took its branch differently on the CI's AMD EPYC 7763 (7 cases) than on the Intel host and the core, so the generator no longer makes them (PAUSE, F3 90, aside); the environment- and privilege-bound instructions left out, whose protected-mode meaning hangs on the host OS's GDT/LDT (their real-mode meaning is SST's); and 16-bit-addressed memory accesses, which always land below 0x10000 on the host and so compare only their fault paths (SST covers the successful ones).*
