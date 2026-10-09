# rex86

![Language](https://img.shields.io/badge/C%2B%2B-20-00599C)
![Hosts](https://img.shields.io/badge/hosts-x86%20%7C%20x86--64%20%7C%20wasm32%20%7C%20AArch64-0078D4)
![Status](https://img.shields.io/badge/status-experimental-orange)
![License](https://img.shields.io/badge/license-BSD--3--Clause-blue)

rex86은 [rePIU](https://github.com/nworkers/rePIU)와 [re2DJ](https://github.com/nworkers/re2DJ)가 함께 쓰는 IA-32 사용자 모드 CPU 코어 라이브러리입니다. 두 프로젝트는 원본 32비트 x86 게임 실행 파일을 고치지 않고 그대로 실행하며 주변 환경만 HLE로 대체합니다. 호스트 CPU가 x86인 데스크톱에서는 원본 바이트를 직접 실행하지만, 브라우저(WebAssembly)와 ARM 기기에서는 그럴 수 없습니다. rex86은 그 호스트들에 CPU를 제공합니다.

코어는 CPU만 압니다. 게스트 실행 형식(LE, PE32), 운영체제(DOS, Win32), 그래픽 API, 자산은 소비자 프로젝트가 호스트 계약(`rex86::Environment`)으로 공급합니다.

현재 버전은 [VERSION](VERSION)에서 확인할 수 있습니다.

*rex86 is the IA-32 user-mode CPU core library shared by [rePIU](https://github.com/nworkers/rePIU) and [re2DJ](https://github.com/nworkers/re2DJ). Both run original 32-bit x86 game executables unmodified, replacing only the surrounding environment with HLE. On desktop hosts whose CPU is x86 they execute the original bytes directly; a browser (WebAssembly) and an ARM device cannot, and rex86 gives those hosts a CPU. The core knows the CPU alone: the guest executable format (LE, PE32), the operating system (DOS, Win32), the graphics API and the assets are supplied by the consumer projects through the host contract (`rex86::Environment`). See [VERSION](VERSION) for the current version.*

> [!WARNING]
> 개발 중입니다. 실행 엔진은 인터프리터 하나이며, 대상 기판 CPU의 상한(P6 정수, P6 x87, MMX, Pentium III SSE)을 모두 구현했습니다. 386 정수 명령과 그 예외 의미는 [SingleStepTests/80386](https://github.com/SingleStepTests/80386) 실측과 대조해 통과합니다(실행 1,741,900건, 불일치 0). 32비트 정수 명령(P6 포함), 80비트 x87(초월함수 포함), MMX와 SSE는 각각 호스트 CPU와 대조해 불일치 0이고, 그 기대값의 일부를 trace로 저장해 다섯 호스트에서 재생합니다. 번역 백엔드(wasm, AArch64 JIT)는 아직 없고, 소비자(rePIU, re2DJ) 통합 전입니다.
>
> *Work in progress. The only execution engine is the interpreter, and it implements the whole target-board CPU ceiling (P6 integer, P6 x87, MMX, the Pentium III's SSE). The 386 integer set and its exception semantics pass against the [SingleStepTests/80386](https://github.com/SingleStepTests/80386) hardware measurements (1,741,900 executed tests, zero mismatches); the 32-bit integer set (P6 included), the 80-bit x87 (transcendentals included), MMX and SSE each pass against host CPUs with zero mismatches, and part of those expectations is kept as traces that all five hosts replay. The translation backends (wasm and AArch64 JIT) do not exist yet, and the consumers (rePIU, re2DJ) are not integrated.*

## 목표와 달성도 / Goals and status

이 프로젝트가 완료로 인정받으려면 아래 아홉 목표를 모두 달성해야 합니다. 각 목표는 이 저장소 단독으로(소비자 프로젝트 없이) 측정할 수 있어야 하며, 측정 수단이 없는 목표는 측정 수단을 만드는 것까지가 목표에 포함됩니다.

*The project is complete only when all nine goals below are met. Every goal must be measurable by this repository alone (without the consumer projects); where a measuring instrument does not exist, building it is part of the goal.*

### 1. 명령 지원 / Instruction coverage

대상 기판(안다미로 MK3, MK5, EZ2DJ 1세대, 2세대)의 CPU가 실행할 수 있는 **모든 사용자 모드 CPU/FPU 명령을 지원**합니다. 범위는 그 CPU들의 합집합 상한인 **P6 정수 명령 전체, P6 x87 전체(80비트 소프트웨어 구현, 초월함수 포함), MMX, Pentium III의 SSE(FXSAVE/FXRSTOR 포함)**입니다. SSE2 이후는 어느 기판에도 없어 범위 밖이고, K6-2에만 있는 3DNow!는 EZ2DJ 1세대 기판의 게임(1st ~ 6th TRAX) census가 사용을 확인할 때만 넣습니다. 기판별 CPU는 소비자가 `Features`로 흉내 냅니다(근거: [대상 기판의 CPU](docs/kb/target-board-cpus.md), [#21 설계](docs/design/20261008-i021-target-board-cpu-baseline.md)). 의미의 기준은 Intel SDM입니다.

명령 의미에는 **주소 생성과 세그먼테이션 의미**가 포함됩니다: 세그먼트 base/limit 검사와 #GP 생성, 16비트 세그먼트의 64KiB wrap, 주소 크기 prefix(0x67)의 wrap, 스택 wrap, limit 밖 접근의 폴트 종류. 이는 census에 잡히지 않는 횡단 관심사이므로 전용 테스트로 측정합니다.

* **측정 수단**: 게스트 census 대비 커버리지 리포트(census 도구는 1단계에 이 저장소로 이식), 명령 단위 테스트, 호스트 CPU 대조 fuzz, 주소·세그먼테이션 경계값 전용 테스트.
* **완료 기준**: 위 범위의 명령 형태 전체 구현, 두 소비자 census의 명령 집합 100% 커버, fuzz에서 호스트 CPU와 불일치 0건(SDM이 미정의로 두는 동작 제외), 주소 변환 경계 테스트(wrap, limit, 폴트 종류) 통과.

*Support **every user-mode CPU/FPU instruction the target boards' CPUs can execute** (Andamiro MK3 and MK5, EZ2DJ generations 1 and 2): their union's ceiling, **the whole P6 integer set, the whole P6 x87 (an 80-bit software implementation, transcendentals included), MMX and the Pentium III's SSE (FXSAVE/FXRSTOR included)**. SSE2 and later exist on none of the boards and are out of scope; 3DNow!, the K6-2's alone, enters only if a census of the generation 1 EZ2DJ board's games (1st through 6th TRAX) confirms its use. Consumers emulate each board's CPU through `Features` (see [the target boards' CPUs](docs/kb/target-board-cpus.md) and the [#21 design](docs/design/20261008-i021-target-board-cpu-baseline.md)). The Intel SDM defines the meaning, **address generation and segmentation semantics included**: base/limit checks and #GP, the 16-bit 64KiB wrap, the address-size-prefix wrap, stack wrap and the fault kinds for out-of-limit accesses, a cross-cutting concern the census cannot see, measured by dedicated boundary tests. Measured by a coverage report against the guest census (the census tool moves into this repository in phase 1), per-instruction unit tests, the host-CPU comparison fuzz and the address/segmentation boundary tests. Done when every instruction form in that scope is implemented, both consumers' census sets are covered 100%, the fuzz shows zero divergence from the host CPU (behavior the SDM leaves undefined excluded) and the boundary tests pass.*

### 2. 정확성 / Correctness

인터프리터가 정확성 기준이고, 모든 번역 백엔드는 인터프리터와 **비트 단위로 같은 결과**를 냅니다. 같은 입력은 모든 호스트(x86, x86-64, wasm32, AArch64)에서 같은 결과를 냅니다.

* **측정 수단**: trace 재생 비교(인터프리터 vs 백엔드, 호스트 간), 다섯 호스트 CI, [SingleStepTests/80386](https://github.com/SingleStepTests/80386)(MIT, 실제 하드웨어 생성 테스트 — 소비자와 무관한 독립 기준이고 x86이 아닌 호스트에서도 돈다), SMC 전용 테스트.
* **완료 기준**: 모든 trace에서 상태 비교 일치, 다섯 호스트 CI 녹색, SingleStepTests 실행 비교 통과, **SMC: `kTranslated` 페이지에 쓰기가 일어난 뒤 이전 번역이 절대 실행되지 않음**.

*The interpreter is the correctness reference; every translation backend produces **bit-identical results**, and the same input gives the same result on every host (x86, x86-64, wasm32, AArch64). Measured by trace-replay comparison (interpreter vs. backend, host vs. host), the five-host CI, [SingleStepTests/80386](https://github.com/SingleStepTests/80386) (MIT, hardware-generated tests — a consumer-independent reference that runs on non-x86 hosts too) and dedicated SMC tests; done when every trace compares equal, CI is green on all five hosts, the SingleStepTests execute-and-compare passes, and **after a write to a `kTranslated` page a stale translation never executes**.*

### 3. 성능 / Performance

성능은 실용 기준과 비교군 기준을 모두 충족해야 합니다.

* **실용 기준**: 소비자 게스트급 워크로드가 중급 모바일 브라우저와 ARM 기기에서 **원래 속도(실시간)** 로 돈다. 실시간은 **그 게임이 돌던 기판**의 CPU를 기준으로 판정하고, 기판에 CPU 변형이 여럿이면 **가장 빠른 변형**을 기준으로 합니다. 네 기판 중 가장 빠른 EZ2DJ 2세대(Tualatin Celeron 1.4 GHz)의 게임도 실시간이어야 합니다. 구체적으로, 대표 합성 워크로드가 프레임 예산(16.7ms) 안에서 기준 CPU 한 프레임 분량의 명령을 retire한다.

  | 기판 | 이 기판의 게임 | 기준 CPU(가장 빠른 변형) |
  |---|---|---|
  | EZ2DJ 1세대 | EZ2DJ 1st ~ 6th TRAX | AMD K6-2 400 MHz |
  | 안다미로 MK3 | PIU 1st ~ PREX 3 | Mendocino Celeron 400 MHz |
  | 안다미로 MK5 | Premiere 3, PREX 3(MK5판), Exceed | Tualatin Celeron 1.3 GHz |
  | EZ2DJ 2세대 | EZ2DJ 7th TRAX ~ EZ2AC EVOLVE | Tualatin Celeron 1.4 GHz |
* **비교군 기준**: 같은 워크로드에서 공개된 비전염성 라이선스 대안이 없으므로, 비교군은 측정 가능한 공개 구현(v86, Boxedwine 등)으로 두고 **동등 이상**의 처리량을 목표로 한다. 비교는 벤치마크 결과로만 하며, 전염성 라이선스 구현의 소스는 참고하지 않는다.
* **달성 수단**: wasm/AArch64 **JIT 번역 백엔드**, 블록 체이닝, 플래그 지연 계산(lazy flags), 코드 캐시, SMC의 페이지 속성표 검사 등 현대 바이너리 번역 기법을 적용합니다. JIT 금지 호스트(iOS 네이티브)는 인터프리터 전용 모드가 하한 성능을 책임지고, **AOT**(사전 번역) 경로는 2단계 측정 뒤 rePIU AOT 대체 판단과 함께 검토합니다.
* **측정 수단**: 이 저장소의 벤치마크 하네스(합성 워크로드, MIPS와 프레임 시간 리포트). 측정 없이 최적화하지 않습니다.
* **완료 기준**: 실용 기준 충족 + 공개 비교군 대비 동등 이상. 수치 목표는 census와 2단계 측정 뒤 이 절에 확정해 적습니다.

*Performance must meet both a practical bar and a comparative bar. Practical: consumer-class workloads run at **original (real-time) speed** on a mid-range mobile browser and ARM device; real time is judged against the CPU of **the board the game ran on**, taking **the fastest variant** where a board shipped with several, and the games of the fastest board, generation 2 EZ2DJ (Tualatin Celeron 1.4 GHz), must run in real time too: a representative synthetic workload retires one frame's worth of the reference CPU's instructions within the 16.7 ms frame budget (EZ2DJ generation 1, 1st through 6th TRAX: K6-2 400 MHz; MK3, PIU 1st through PREX 3: Mendocino Celeron 400 MHz; MK5, Premiere 3, the MK5 PREX 3 and Exceed: Tualatin Celeron 1.3 GHz; EZ2DJ generation 2, 7th TRAX through EZ2AC EVOLVE: Tualatin Celeron 1.4 GHz). Comparative: measured against public implementations (v86, Boxedwine, …) on the same workload, targeting **parity or better** throughput; the comparison is benchmark-only and no copyleft source is consulted. The means are modern binary-translation techniques, the wasm/AArch64 **JIT backends**, block chaining, lazy flags, the code cache, page-attribute SMC checks, with the interpreter-only mode as the floor on JIT-forbidden hosts and an **AOT** path evaluated after the phase 2 measurements. Measured by this repository's benchmark harness (synthetic workloads, MIPS and frame-time reports); nothing is optimized without a measurement. Done when the practical bar holds and public comparators are matched or beaten; the numeric targets are fixed into this section after the census and phase 2 measurements.*

### 4. 단독 측정 가능성 / Standalone measurability

위의 모든 달성도는 **이 저장소 단독으로 측정**됩니다. 테스트, fuzz, trace, 벤치마크는 실행 중에 만든 합성 코드로 돌며 원본 게임 바이너리를 요구하지 않습니다.

* **완료 기준**: `ctest` 한 번(호스트별 preset)으로 정확성 지표가, 벤치마크 하네스 한 번으로 성능 지표가 산출.

*Every attainment above is **measured by this repository alone**: tests, fuzz, traces and benchmarks run on synthetic code generated at run time and require no original game binary. Done when one `ctest` run (per-host preset) yields the correctness figures and one benchmark-harness run yields the performance figures.*

### 5. 웹 실행 예제 / Web demo

브라우저에서 바로 실행되는 **공개 예제 페이지**를 제공합니다. wasm으로 빌드한 코어가 합성 게스트 워크로드를 실행하고, 엔진 선택(인터프리터/wasm 백엔드), retire 수, 처리량을 화면에 보여줍니다.

* **완료 기준**: 데스크톱과 모바일 브라우저에서 접속만으로 실행되고, 위 벤치마크 지표를 같은 페이지에서 재현.

*Provide a **public demo page** that runs in the browser: the wasm-built core executes a synthetic guest workload and shows the engine choice (interpreter / wasm backend), retired-instruction counts and throughput. Done when it runs on desktop and mobile browsers by simply opening the page, reproducing the benchmark figures above.*

### 6. 견고성과 격리 / Robustness and sandboxing

코어의 입력은 임의의 원본 바이너리이고 웹에서는 신뢰할 수 없는 입력입니다. **어떤 바이트열과 어떤 상태를 주어도 호스트 프로세스는 크래시하지 않고, 게스트는 `GuestMemory` 경계 밖을 읽거나 쓸 수 없습니다.** 성립하지 않는 입력은 명시적 폴트 이벤트로 보고됩니다.

* **측정 수단**: 디코더·인터프리터·번역기 fuzzing(임의 바이트열 + 임의 상태), ASan/UBSan을 켠 CI 작업.
* **완료 기준**: fuzz에서 크래시·경계 밖 접근·sanitizer 보고 0건. 웹 데모(목표 5) 공개 전에 성립해야 합니다.

*The core's input is an arbitrary original binary — on the web, untrusted input. **No byte sequence and no state may crash the host process, and the guest can never read or write beyond `GuestMemory`**; invalid input is reported as an explicit fault event. Measured by fuzzing the decoder, interpreter and translators (arbitrary bytes and state) and a CI job under ASan/UBSan; done at zero crashes, zero out-of-bounds accesses and zero sanitizer reports — and it must hold before the web demo (goal 5) ships.*

### 7. 자원 예산 / Resource budget

1차 목표가 모바일 브라우저이므로 크기와 메모리에 수치 상한을 둡니다: wasm 모듈 크기, 런타임 메모리 상한, 코드 캐시 예산. 코드 캐시는 예산 안에서 축출(eviction)하며 축출이 정확성을 해치지 않습니다.

* **측정 수단**: CI의 빌드 산출물 크기 리포트, 벤치마크 하네스의 메모리 계측, 코드 캐시 축출 테스트.
* **완료 기준**: 수치 상한은 4단계(웹 호스트) 측정에서 확정해 이 절에 적습니다. 그때까지 CI가 크기 추세를 기록합니다.

*The first target is the mobile browser, so size and memory get numeric caps: the wasm module size, the runtime memory ceiling and the code-cache budget, with eviction inside the budget that never harms correctness. Measured by CI artifact-size reports, the benchmark harness's memory instrumentation and code-cache eviction tests; the caps are fixed into this section at the phase 4 measurements, with CI recording the size trend until then.*

### 8. 반응성과 계층화 / Responsiveness and tiering

목표 3(처리량)과 별도로 지연 목표를 둡니다: 인터프리터가 즉시 실행을 시작하고 번역 백엔드는 비동기로 워밍업하며, **번역 때문에 프레임이 멈추지 않습니다**(p99 프레임 시간). 첫 프레임까지의 시간도 측정합니다.

* **측정 수단**: 벤치마크 하네스의 프레임 시간 분포(p99)와 time-to-first-frame 계측.
* **완료 기준**: 수치 목표는 목표 3과 같은 시점(2단계 측정 뒤)에 확정합니다. 구조 기준은 지금부터 적용됩니다: 번역은 실행을 블록하지 않습니다.
* **예산의 단위**: `Run(budget)`의 예산은 단계 단위이고, REP 문자열은 반복 하나가 한 단계입니다([#32](https://github.com/reexec/rex86/issues/32)). 그래서 긴 REP 문자열 도중에도 `Run`은 예산에서 돌아오고, `RequestStop`과 대기 인터럽트를 반복 사이에서 처리합니다.

*Separately from goal 3's throughput, latency targets: the interpreter starts executing immediately, the translation backends warm up asynchronously, and **translation never stalls a frame** (p99 frame time), with time-to-first-frame measured. Measured by the benchmark harness's frame-time distribution and first-frame instrumentation; the numbers are fixed with goal 3's after the phase 2 measurements, while the structural bar — translation never blocks execution — applies from now. The budget of `Run(budget)` counts steps, one per REP string iteration (#32), so `Run` returns at its budget even inside a long REP string and handles `RequestStop` and pending interrupts between iterations.*

### 9. 관측성 / Observability

trace 기록·재생, 단일 스텝, 상태 덤프를 코어가 제공합니다. 목표 2의 측정이 여기 의존하고, 소비자가 게스트 문제를 디버깅할 때의 실질 수단입니다.

* **완료 기준**: **모든 불일치(백엔드 대 인터프리터, 호스트 간)는 trace로 재현 가능합니다.** 재현 불가능한 불일치는 관측성의 버그로 셉니다.

*The core provides trace record/replay, single-stepping and state dumps — what goal 2's measurement depends on and what consumers debug guests with. Done when **every divergence (backend vs. interpreter, host vs. host) reproduces from a trace**; a divergence that cannot be reproduced counts as an observability bug.*

### 비목표 / Non-goals

다음은 목표가 아닙니다(근거: [프로젝트 헌장](docs/PROJECT_CHARTER.md)): **게스트 메모리의 할당과 배치**(소비자가 `GuestMemory`를 공급합니다), **게스트 페이징**(CR3, 페이지 테이블 — 사용자 모드 코어입니다), **멀티스레드 게스트 동시 실행**(두 소비자 모두 한 번에 한 게스트 스레드), 게임 로직·로더·HLE·그래픽(소비자의 것).

*Non-goals (see the [project charter](docs/PROJECT_CHARTER.md)): **guest-memory allocation and placement** (consumers supply `GuestMemory`), **guest paging** (CR3 and page tables — this is a user-mode core), **concurrent multithreaded guests** (both consumers run one guest thread at a time), and game logic, loaders, HLE and graphics (the consumers').*

### 달성도 / Attainment

단계 정의는 [설계 문서](docs/design/20261007-i001-repository-and-public-contract.md)의 단계 계획을 따릅니다. 이 표는 작업이 머지될 때마다 갱신합니다.

| 단계 / Phase | 산출물 / Deliverable | 관련 목표 / Goals | 상태 / Status |
|---|---|---|---|
| 0 | 저장소, 공개 계약, 검증 하네스, 다섯 호스트 CI / repository, public contract, harness, five-host CI | 2, 4 | ✅ 완료 / done ([#1](https://github.com/reexec/rex86/issues/1), v0.0.2) |
| 1 | 디코더, 인터프리터, 80비트 x87, census 이식, 대조 fuzz, trace / decoder, interpreter, 80-bit x87, census port, comparison fuzz, trace | 1, 2, 4, 9 | ✅ 완료 / done: 디코더와 census 도구([#5](https://github.com/reexec/rex86/issues/5)), SingleStepTests 러너([#7](https://github.com/reexec/rex86/issues/7)), 인터프리터 1~3차: **386 정수 명령 전체**([#11](https://github.com/reexec/rex86/issues/11), [#13](https://github.com/reexec/rex86/issues/13), [#15](https://github.com/reexec/rex86/issues/15)), **예외 의미**: #GP/#SS/#UD/#DE/BOUND와 INT n을 IVT 전달까지 비교, 정확한 폴트([#17](https://github.com/reexec/rex86/issues/17)). SST 실행 174만 테스트 불일치 0, 미구현 0. **x87 1차**: SoftFloat 3e 위의 80비트 x87(초월함수 제외)과 x87 호스트 CPU 대조 fuzz, 4,000만 건 불일치 0([#19](https://github.com/reexec/rex86/issues/19)). **P6 정수, 정수 대조 fuzz, trace**: 다음 행([#22](https://github.com/reexec/rex86/issues/22)). **x87 2차**: 초월함수 여덟, 66비트 Pi 축소와 정확한 반올림, ulp 허용치 대조와 독립 오라클([#25](https://github.com/reexec/rex86/issues/25)) / decoder, census tool, SingleStepTests runner, interpreter increments 1-3 (**the whole 386 integer set**), **exception semantics** (#GP/#SS/#UD/#DE/BOUND and INT n compared through IVT delivery, precise faults, #17): 1.74M SST tests executed, zero mismatches, zero unimplemented. **x87 increment 1**: the 80-bit x87 on SoftFloat 3e, transcendentals excluded, with the x87 host-CPU comparison fuzz at 40M cases and zero mismatches (#19). **P6 integer, the integer comparison fuzz, traces**: the next row (#22). **x87 increment 2**: the eight transcendentals, reduced by the 66-bit Pi and correctly rounded, compared within an ulp tolerance and checked by an independent oracle (#25) |
| 1 | P6 정수 보완(CMOVcc, BSWAP, XADD, CMPXCHG, CMPXCHG8B, UD2, EFLAGS AC/ID), 정수 명령 호스트 대조 fuzz, trace / P6 integer completion, the integer host-comparison fuzz, trace | 1, 2, 9 | ✅ 완료 / done ([#22](https://github.com/reexec/rex86/issues/22)): i386 TF 단일 스텝 대조 1,000만 건 불일치 0, trace 묶음 14,587건을 다섯 호스트가 재생, `Features::cmov` / the i386 trap-flag comparison at 10M cases with zero mismatches, a 14,587-case trace corpus replayed by the five hosts, `Features::cmov` |
| 1b | MMX, SSE(Pentium III, FXSAVE/FXRSTOR 포함) / MMX and SSE (Pentium III, FXSAVE/FXRSTOR included) | 1, 2 | ✅ 완료 / done ([#29](https://github.com/reexec/rex86/issues/29)): MMX 전체, SSE가 더한 MMX 정수 명령, SSE 이동/논리/셔플, SSE 부동소수점(MXCSR 예외와 #XM), FXSAVE/FXRSTOR, 기판별 `Features`(`mmx`, `fxsr`, `sse`). AMD Zen 3 대조 2,200만 건 불일치 0, trace 묶음 `simd.rxt` / all of MMX, the SSE-added MMX integer instructions, SSE moves/logic/shuffles, SSE floating point (MXCSR exceptions and #XM), FXSAVE/FXRSTOR, per-board `Features`; 22M cases against an AMD Zen 3 with zero mismatches, the `simd.rxt` corpus |
| 2A, 2B | 소비자 통합(각 소비자 저장소) / consumer integration (in each consumer repo) | 2 | ⬜ 미착수 / not started |
| 3 | IR 프런트엔드, wasm JIT 백엔드, SMC 검사 / IR frontend, wasm JIT backend, SMC checks | 2, 3, 8 | ⬜ 미착수 / not started |
| — | 벤치마크 하네스와 비교군 측정 / benchmark harness and comparator measurement | 3, 4, 7, 8 | 🔶 진행 중 / in progress: 하네스 `rex86_bench`(합성 워크로드 6종, MIPS, 프레임 시간 p50/p99, 첫 프레임, 기판 기준 실시간 비율)와 인터프리터 첫 기준선([#27](https://github.com/reexec/rex86/issues/27), [측정](docs/analysis/interpreter-performance.md)). 비교군(v86, Boxedwine) 측정은 웹 데모 뒤 / the harness `rex86_bench` (six synthetic workloads, MIPS, frame-time p50/p99, first frame, per-board real-time ratio) and the interpreter's first baseline (#27); comparator measurement follows the web demo |
| - | 견고성 하네스(fuzzing, ASan/UBSan CI) / robustness harness (fuzzing, ASan/UBSan CI) | 6 | ✅ 완료 / done (디코더, 인터프리터 / decoder, interpreter): `rex86_robust`(임의 바이트열, 상태, 페이지 속성, 호스트 응답과 불변식 여섯), libFuzzer 진입점과 CI 작업 `linux-x64-libfuzzer`, ASan/UBSan CI([#22](https://github.com/reexec/rex86/issues/22), [#31](https://github.com/reexec/rex86/issues/31), [기록](docs/analysis/robustness-fuzz.md)). 번역 백엔드는 3단계에서 같은 하네스로 / `rex86_robust` (arbitrary bytes, state, page attributes and host answers against six invariants), a libFuzzer entry and the `linux-x64-libfuzzer` CI job, ASan/UBSan CI; translation backends join the same harness in phase 3 |
| 4 | 웹 실행 예제, 자원 예산 확정 / web demo, resource caps fixed | 5, 6, 7 | ⬜ 미착수 / not started |
| 5 | AArch64 JIT 백엔드 / AArch64 JIT backend | 1, 3 | ⬜ 미착수 / not started |
| 6 | ARM 네이티브 호스트(소비자 저장소) / ARM native hosts (consumer repos) | 3 | ⬜ 미착수 / not started |

현재 수치(#32 기준): SingleStepTests/80386 real mode 941 파일 1,758,700 테스트 중 **1,741,900건 실행, 불일치 0, 미구현 0**(예외 기록 테스트 149,100건 포함). 실행하지 않은 16,800건은 설계상 경계(포트 입력, 특권 명령 8,893), SDM과 다른 386EX 동작(무효 SIB 등 7,903), 하네스로 표현할 수 없는 경우(4)입니다. 32비트 정수 명령은 1,112개 형태(149개 mnemonic, P6까지)를 Intel Cascade Lake의 i386 프로세스와 대조해 **1,000만 건 불일치 0**입니다([분석](docs/analysis/integer-host-comparison.md)). x87은 초월함수를 뺀 473개 인코딩을 AMD Zen 3(4,000만 건)와 Intel Cascade Lake(2,000만 건)에 대조해 **불일치 0**이고, 두 제조사 모두의 SDM 이탈(마스크 안 된 #IA 비교, 0.63%)과 AMD만의 이탈은 따로 셉니다([분석](docs/analysis/x87-host-comparison.md)). 초월함수 여덟은 66비트 Pi 축소와 정확한 반올림으로 구현해 Intel Kaby Lake와 2,000만 건 대조에서 **불일치 0**이고(1 ulp 이내 또는 C1만 다른 허용 사례 6.88%, 2 ulp 0), 독립 오라클로 본 6만 건에서 코어는 모두 정확히 반올림했습니다. MMX와 SSE는 형태 7,961개를 AMD Zen 3와 2,200만 건 대조해 **불일치 0**이고(RCPPS/RSQRTPS의 근사값 차이는 SDM 한계 안의 허용으로 따로 셈), SSE가 x87과 다르게 반올림 전에 언더플로를 판정하는 것 같은 측정 결과는 [분석](docs/analysis/simd-host-comparison.md)에 있습니다. 저장소의 trace 묶음(정수 12,000, x87 2,587, 초월함수 1,850, SIMD)은 다섯 호스트에서 재생됩니다. 성능은 벤치마크 하네스 `rex86_bench`([#27](https://github.com/reexec/rex86/issues/27))로 잽니다: 인터프리터는 디코드 캐시([#34](https://github.com/reexec/rex86/issues/34))와 블록 루프([#35](https://github.com/reexec/rex86/issues/35)) 뒤 AMD Ryzen 5 5600X에서 x86-64 GCC Release 12.5 MIPS(혼합 워크로드, #34 전 3.6), Windows x86 MSVC Release 9.1 MIPS(같은 날 #35 전 7.3)이고, 기판 기준 CPU(IPC 1.0 가정) 대비 실시간 비율은 400 MHz 기판에 0.031, 1.4 GHz 기판에 0.0089입니다([측정](docs/analysis/interpreter-performance.md)). #32부터 예산과 MIPS는 단계(REP 반복 하나가 한 단계)를 세므로 `string`과 혼합 워크로드의 수치는 그 전과 바로 비교하지 않습니다. 소비자 census 대비 커버리지는 아직 없습니다.

*Phases follow the [design document](docs/design/20261007-i001-repository-and-public-contract.md)'s plan; the table is updated as work merges. Current figures (as of #32): of SingleStepTests/80386's 1,758,700 real-mode tests in 941 files, **1,741,900 executed with zero mismatches and zero unimplemented** (149,100 exception-recording tests included). The 16,800 not executed are design boundaries (port input and privileged instructions, 8,893), 386EX behavior deviating from the SDM (invalid SIB and others, 7,903) and cases the harness cannot represent (4). The 32-bit integer set, 1,112 forms (149 mnemonics, up to the P6), compares against an i386 process on an Intel Cascade Lake at **10M cases with zero mismatches** ([analysis](docs/analysis/integer-host-comparison.md)). The x87, 473 encodings without the transcendentals, compares against an AMD Zen 3 (40M cases) and an Intel Cascade Lake (20M) with **zero mismatches**, the SDM deviation both vendors share (unmasked-#IA compares, 0.63%) and AMD's own counted apart ([analysis](docs/analysis/x87-host-comparison.md)). The eight transcendentals, reduced by the 66-bit Pi and correctly rounded, compare against an Intel Kaby Lake at 20M cases with **zero mismatches** (6.88% tolerated as within 1 ulp or differing in C1 alone, none at 2 ulps), and an independent oracle finds the core correctly rounded on all 60,000 cases it checked. MMX and SSE, 7,961 forms, compare against an AMD Zen 3 at 22M cases with **zero mismatches** (RCPPS/RSQRTPS approximation differences counted apart as tolerated within the SDM's bound), measured facts such as SSE detecting tininess before rounding, unlike the x87, being in the [analysis](docs/analysis/simd-host-comparison.md). The committed trace corpus (12,000 integer, 2,587 x87, 1,850 transcendental, and the SIMD cases) replays on all five hosts. Performance is measured by the benchmark harness `rex86_bench` (#27): after the decode cache (#34) and the block loop (#35) the interpreter runs the mixed workload at 12.5 MIPS in the x86-64 GCC Release build (3.6 before #34) and 9.1 in the Windows x86 MSVC Release build (7.3 before #35 on the same day) on an AMD Ryzen 5 5600X, a real-time ratio against the boards' reference CPUs (IPC 1.0 assumed) of 0.031 for the 400 MHz boards and 0.0089 for the 1.4 GHz board ([analysis](docs/analysis/interpreter-performance.md)). From #32 on the budget and MIPS count steps (one per REP iteration), so the `string` and mixed figures do not compare directly with those before. Coverage against the consumer census does not exist yet.*

## 구조 / Layout

| 경로 / Path | 내용 / Contents |
|---|---|
| `include/rex86/` | 공개 계약: `cpu_state.h`, `guest_memory.h`, `environment.h`, `cpu.h`, `version.h` / the public contract |
| `src/` | 코어 구현. OS 헤더를 포함하지 않음 / the core, with no OS header |
| `src/tools/probe/` | `rex86_probe`: 모든 호스트에서 같은 줄을 찍는 확인 도구 / prints the same lines on every host |
| `src/tools/census/` | `rex86_census`: 평탄 코드 이미지의 명령 census ([가이드](docs/guides/instruction-census.md)) / the instruction census over a flat code image |
| `third_party/zydis/` | Zydis v4.1.1 amalgamation (MIT, [고지](THIRD_PARTY_NOTICES.md)) / the vendored decoder library |
| `tests/unit/` | 단위 테스트 (외부 프레임워크 없음) / unit tests without a framework |
| `docs/` | 설계, 작업 지시, 작업 로그, 분석, 지식 기반 / design, work orders, work logs, analysis, knowledge base |

## 빌드 / Build

```bash
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug
./build/linux-x64-debug/bin/rex86_probe
```

Windows는 `cmake -S . -B build -A Win32`, Linux i386은 `linux-x86-debug` preset, wasm32는 `scripts/build_web_wasm.sh`(Emscripten 필요)를 씁니다. CI는 Windows x86, Linux x64(GCC, Clang), Linux i386, Linux AArch64, wasm32의 다섯 호스트에서 돕니다.

*Windows uses `cmake -S . -B build -A Win32`, Linux i386 the `linux-x86-debug` preset, and wasm32 `scripts/build_web_wasm.sh` (Emscripten required). CI runs on five hosts: Windows x86, Linux x64 (GCC, Clang), Linux i386, Linux AArch64 and wasm32.*

## 소비자에서 쓰기 / Using it from a consumer

```cmake
FetchContent_Declare(rex86
    GIT_REPOSITORY https://github.com/reexec/rex86.git
    GIT_TAG v0.0.2)
FetchContent_MakeAvailable(rex86)
target_link_libraries(your_target PRIVATE rex86::core)
```

코어와 소비자를 함께 고칠 때는 `-DFETCHCONTENT_SOURCE_DIR_REX86=../rex86`으로 형제 체크아웃을 가리킵니다.

*To change the core and a consumer together, point `-DFETCHCONTENT_SOURCE_DIR_REX86=../rex86` at a sibling checkout.*

## 문서 / Documents

* [AGENTS.md](AGENTS.md): 개발 규칙. rePIU의 규칙에서 CPU 코어에 필요한 것만 추렸습니다 / the development rules, the subset of rePIU's that applies here
* [docs/PROJECT_CHARTER.md](docs/PROJECT_CHARTER.md): 목적, 목표 호스트, 방향, 비목표 / purpose, target hosts, direction, non-goals
* [ARCHITECTURE.md](ARCHITECTURE.md): 지금 구현된 구조와 계획된 구조 / the structure as implemented and as planned
* [docs/design/](docs/design/): 설계 문서. 첫 문서가 공용 계약과 단계 계획입니다 / design documents, the first being the public contract and the phase plan

## 라이선스 / License

BSD 3-Clause. 이 저장소는 원본 게임 바이너리나 데이터를 포함하지 않으며, 테스트는 실행 중에 만든 합성 코드로 합니다.

*BSD 3-Clause. The repository contains no original game binary or data; tests run on synthetic code generated at run time.*
