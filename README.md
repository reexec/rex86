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
> 아직 실행 엔진이 없습니다. #1은 저장소 규칙, 공개 계약(CPU 상태, 게스트 메모리 뷰, 호스트 계약), 검증 하네스, 다섯 호스트의 CI까지입니다. 인터프리터는 다음 작업입니다.
>
> *There is no execution engine yet. #1 delivers the repository rules, the public contract (CPU state, guest memory view, host contract), the verification harness and CI on five hosts. The interpreter is the next task.*

## 목표와 달성도 / Goals and status

이 프로젝트가 완료로 인정받으려면 아래 다섯 목표를 모두 달성해야 합니다. 각 목표는 이 저장소 단독으로(소비자 프로젝트 없이) 측정할 수 있어야 하며, 측정 수단이 없는 목표는 측정 수단을 만드는 것까지가 목표에 포함됩니다.

*The project is complete only when all five goals below are met. Every goal must be measurable by this repository alone (without the consumer projects); where a measuring instrument does not exist, building it is part of the goal.*

### 1. 명령 지원 / Instruction coverage

소비자 게스트(rePIU, re2DJ의 원본 실행 파일)가 실제로 사용하는 **모든 CPU/FPU 명령을 지원**합니다. 범위는 IA-32 사용자 모드 정수 명령 전체와 x87(80비트 소프트웨어 구현), 그리고 census가 확인하는 MMX/SSE 계열입니다. 의미의 기준은 Intel SDM입니다.

* **측정 수단**: 게스트 census 대비 커버리지 리포트(census 도구는 1단계에 이 저장소로 이식), 명령 단위 테스트, 호스트 CPU 대조 fuzz.
* **완료 기준**: 두 소비자 census의 명령 집합 100% 커버, fuzz에서 호스트 CPU와 불일치 0건(SDM이 미정의로 두는 동작 제외).

*Support **every CPU/FPU instruction the consumer guests actually use**: the full IA-32 user-mode integer set, x87 as an 80-bit software implementation, and the MMX/SSE families the census confirms. The Intel SDM defines the meaning. Measured by a coverage report against the guest census (the census tool moves into this repository in phase 1), per-instruction unit tests and the host-CPU comparison fuzz. Done when both consumers' census sets are covered 100% and the fuzz shows zero divergence from the host CPU (behavior the SDM leaves undefined excluded).*

### 2. 정확성 / Correctness

인터프리터가 정확성 기준이고, 모든 번역 백엔드는 인터프리터와 **비트 단위로 같은 결과**를 냅니다. 같은 입력은 모든 호스트(x86, x86-64, wasm32, AArch64)에서 같은 결과를 냅니다.

* **측정 수단**: trace 재생 비교(인터프리터 vs 백엔드, 호스트 간), 다섯 호스트 CI.
* **완료 기준**: 모든 trace에서 상태 비교 일치, 다섯 호스트 CI 녹색.

*The interpreter is the correctness reference; every translation backend produces **bit-identical results**, and the same input gives the same result on every host (x86, x86-64, wasm32, AArch64). Measured by trace-replay comparison (interpreter vs. backend, host vs. host) and the five-host CI; done when every trace compares equal and CI is green on all five hosts.*

### 3. 성능 / Performance

성능은 실용 기준과 비교군 기준을 모두 충족해야 합니다.

* **실용 기준**: 소비자 게스트급 워크로드가 중급 모바일 브라우저와 ARM 기기에서 **원래 속도(실시간)** 로 돈다. 구체적으로, 대표 합성 워크로드가 프레임 예산(16.7ms) 안에서 원본 하드웨어(Pentium II~III급) 한 프레임 분량의 명령을 retire한다.
* **비교군 기준**: 같은 워크로드에서 공개된 비전염성 라이선스 대안이 없으므로, 비교군은 측정 가능한 공개 구현(v86, Boxedwine 등)으로 두고 **동등 이상**의 처리량을 목표로 한다. 비교는 벤치마크 결과로만 하며, 전염성 라이선스 구현의 소스는 참고하지 않는다.
* **달성 수단**: wasm/AArch64 **JIT 번역 백엔드**, 블록 체이닝, 플래그 지연 계산(lazy flags), 코드 캐시, SMC의 페이지 속성표 검사 등 현대 바이너리 번역 기법을 적용합니다. JIT 금지 호스트(iOS 네이티브)는 인터프리터 전용 모드가 하한 성능을 책임지고, **AOT**(사전 번역) 경로는 2단계 측정 뒤 rePIU AOT 대체 판단과 함께 검토합니다.
* **측정 수단**: 이 저장소의 벤치마크 하네스(합성 워크로드, MIPS와 프레임 시간 리포트). 측정 없이 최적화하지 않습니다.
* **완료 기준**: 실용 기준 충족 + 공개 비교군 대비 동등 이상. 수치 목표는 census와 2단계 측정 뒤 이 절에 확정해 적습니다.

*Performance must meet both a practical bar and a comparative bar. Practical: consumer-class workloads run at **original (real-time) speed** on a mid-range mobile browser and ARM device — a representative synthetic workload retires one original-hardware frame's worth (Pentium II–III class) of instructions within the 16.7 ms frame budget. Comparative: measured against public implementations (v86, Boxedwine, …) on the same workload, targeting **parity or better** throughput; the comparison is benchmark-only and no copyleft source is consulted. The means are modern binary-translation techniques — the wasm/AArch64 **JIT backends**, block chaining, lazy flags, the code cache, page-attribute SMC checks — with the interpreter-only mode as the floor on JIT-forbidden hosts and an **AOT** path evaluated after the phase 2 measurements. Measured by this repository's benchmark harness (synthetic workloads, MIPS and frame-time reports); nothing is optimized without a measurement. Done when the practical bar holds and public comparators are matched or beaten; the numeric targets are fixed into this section after the census and phase 2 measurements.*

### 4. 단독 측정 가능성 / Standalone measurability

위의 모든 달성도는 **이 저장소 단독으로 측정**됩니다. 테스트, fuzz, trace, 벤치마크는 실행 중에 만든 합성 코드로 돌며 원본 게임 바이너리를 요구하지 않습니다.

* **완료 기준**: `ctest` 한 번(호스트별 preset)으로 정확성 지표가, 벤치마크 하네스 한 번으로 성능 지표가 산출.

*Every attainment above is **measured by this repository alone**: tests, fuzz, traces and benchmarks run on synthetic code generated at run time and require no original game binary. Done when one `ctest` run (per-host preset) yields the correctness figures and one benchmark-harness run yields the performance figures.*

### 5. 웹 실행 예제 / Web demo

브라우저에서 바로 실행되는 **공개 예제 페이지**를 제공합니다. wasm으로 빌드한 코어가 합성 게스트 워크로드를 실행하고, 엔진 선택(인터프리터/wasm 백엔드), retire 수, 처리량을 화면에 보여줍니다.

* **완료 기준**: 데스크톱과 모바일 브라우저에서 접속만으로 실행되고, 위 벤치마크 지표를 같은 페이지에서 재현.

*Provide a **public demo page** that runs in the browser: the wasm-built core executes a synthetic guest workload and shows the engine choice (interpreter / wasm backend), retired-instruction counts and throughput. Done when it runs on desktop and mobile browsers by simply opening the page, reproducing the benchmark figures above.*

### 달성도 / Attainment

단계 정의는 [설계 문서](docs/design/20261007-i001-repository-and-public-contract.md)의 단계 계획을 따릅니다. 이 표는 작업이 머지될 때마다 갱신합니다.

| 단계 / Phase | 산출물 / Deliverable | 관련 목표 / Goals | 상태 / Status |
|---|---|---|---|
| 0 | 저장소, 공개 계약, 검증 하네스, 다섯 호스트 CI / repository, public contract, harness, five-host CI | 2, 4 | ✅ 완료 / done ([#1](https://github.com/reexec/rex86/issues/1), v0.0.2) |
| 1 | 디코더, 인터프리터, 80비트 x87, census 이식, 대조 fuzz, trace / decoder, interpreter, 80-bit x87, census port, comparison fuzz, trace | 1, 2, 4 | 🔶 진행 중 / in progress — 디코더와 census 도구 완료 / decoder and census tool done ([#5](https://github.com/reexec/rex86/issues/5)) |
| 2A·2B | 소비자 통합(각 소비자 저장소) / consumer integration (in each consumer repo) | 2 | ⬜ 미착수 / not started |
| 3 | IR 프런트엔드, wasm JIT 백엔드, SMC 검사 / IR frontend, wasm JIT backend, SMC checks | 2, 3 | ⬜ 미착수 / not started |
| — | 벤치마크 하네스와 비교군 측정 / benchmark harness and comparator measurement | 3, 4 | ⬜ 미착수 / not started |
| 4 | 웹 실행 예제 / web demo | 5 | ⬜ 미착수 / not started |
| 5 | AArch64 JIT 백엔드 / AArch64 JIT backend | 1, 3 | ⬜ 미착수 / not started |
| 6 | ARM 네이티브 호스트(소비자 저장소) / ARM native hosts (consumer repos) | 3 | ⬜ 미착수 / not started |

현재 명령 커버리지 0% (엔진 없음), 성능 측정치 없음. 1단계부터 이 절에 수치를 누적합니다.

*Phases follow the [design document](docs/design/20261007-i001-repository-and-public-contract.md)'s plan; the table is updated as work merges. Current instruction coverage is 0% (no engine) and no performance figure exists yet; numbers accumulate in this section from phase 1 on.*

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
