# 프로젝트 헌장

## 목적

rex86은 rePIU와 re2DJ가 호스트 CPU가 x86이 아닌 환경에서 원본 32비트 x86 코드를 실행할 수 있게 하는 IA-32 사용자 모드 CPU 코어 라이브러리를 만든다.

두 소비자 프로젝트의 원칙은 "원본 게임 로직은 원본 코드가 담당하고, C++는 주변 환경만 제공한다"이다. rex86은 그 원칙이 x86이 아닌 호스트에서도 성립하게 하는 부품이다. 코어는 CPU이고, 게스트 형식, 운영체제, 그래픽, 자산은 소비자의 것이다.

## Purpose

rex86 builds the IA-32 user-mode CPU core library that lets rePIU and re2DJ run original 32-bit x86 code on hosts whose CPU is not x86.

Both consumers hold to one principle: the original code owns the game logic, and C++ provides only the surrounding environment. rex86 is the part that keeps that principle true on hosts that are not x86. The core is the CPU; the guest format, the operating system, graphics and assets belong to the consumers.

---

## 지원 목표 호스트

| 호스트 | 게스트 실행 수단 | 코어 엔진 | 상태 |
| --- | --- | --- | --- |
| 브라우저 (데스크톱, Android, iOS Safari) | wasm | 인터프리터, 그 다음 wasm 백엔드 | 1차 목표. 모바일에 닿는 첫 경로 |
| Android 네이티브, Linux arm64 | AArch64 네이티브 | 인터프리터, 그 다음 AArch64 백엔드 | 2차 목표 |
| macOS Apple Silicon, Windows on ARM | AArch64 네이티브 | 같음 (코드 캐시 보호 전환은 호스트 콜백) | 2차 목표 |
| iOS 네이티브 앱 | AArch64, JIT 금지 | 인터프리터만 | 2차 목표 |
| Windows x86, Linux x86, Linux x64, Windows x64 | 소비자의 기존 직접 실행 | 코어는 비교 상대(oracle)와 진단 fallback | 변화 없음 |

코어 자체는 위 모든 호스트에서 빌드되고 같은 결과를 내야 하며, CI가 Windows x86, Linux x64, Linux i386, Linux AArch64, wasm32에서 그것을 확인한다.

## 지원 CPU 사양

최소 지원 사양은 대상 기판의 CPU다: 안다미로 MK3(Pentium II 또는 Celeron 333~366 MHz), MK5(Celeron 1.3 GHz, Tualatin), EZ2DJ 1세대(AMD K6-2 300~400 MHz), 2세대(Celeron 533 MHz 또는 K6-2, 미확정). 코어는 그 합집합의 상한인 **P6 정수, P6 x87, MMX, SSE(Pentium III)**를 구현하고, 소비자는 흉내 낼 기판에 맞춰 `Features`를 끈다. SSE2 이후는 범위 밖이고, 3DNow!는 census가 사용을 확인할 때만 넣는다. 성능의 실시간 기준도 기판별 CPU이며 상한은 MK5다. 근거: [대상 기판의 CPU](kb/target-board-cpus.md), [#21 설계](design/20261008-i021-target-board-cpu-baseline.md).

## Target Hosts

| Host | How the guest runs | Core engine | Status |
| --- | --- | --- | --- |
| Browser (desktop, Android, iOS Safari) | wasm | the interpreter, then the wasm backend | first goal; the first path to mobile |
| Android native, Linux arm64 | AArch64 native | the interpreter, then the AArch64 backend | second goal |
| macOS Apple Silicon, Windows on ARM | AArch64 native | the same (code-cache protection switching through host callbacks) | second goal |
| iOS native app | AArch64, JIT forbidden | the interpreter alone | second goal |
| Windows x86, Linux x86, Linux x64, Windows x64 | the consumers' existing direct execution | the core as comparison oracle and diagnostic fallback | unchanged |

The core itself must build and give the same results on every host above; CI checks that on Windows x86, Linux x64, Linux i386, Linux AArch64 and wasm32.

## Supported CPU Specification

The minimum supported specification is the target boards' CPUs: Andamiro MK3 (Pentium II or Celeron, 333-366 MHz), MK5 (Celeron 1.3 GHz, Tualatin), EZ2DJ generation 1 (AMD K6-2, 300-400 MHz) and generation 2 (Celeron 533 MHz or K6-2, unresolved). The core implements their union's ceiling, **P6 integer, P6 x87, MMX and SSE (Pentium III)**, and consumers turn `Features` off to match the board they emulate. SSE2 and later are out of scope; 3DNow! enters only if a census confirms its use. Real time is also judged per board CPU, the MK5 being the ceiling. See [the target boards' CPUs](kb/target-board-cpus.md) and the [#21 design](design/20261008-i021-target-board-cpu-baseline.md).

---

## 방향성

* 게스트 명령의 의미는 Intel SDM이 기준이다. 호스트 CPU의 동작은 측정 도구이지 기준이 아니다.
* 인터프리터가 정확성 기준이다. 번역 백엔드는 인터프리터와 같은 결과를 내야 한다.
* x86 의미는 인터프리터와 IR 프런트엔드에만 둔다. 백엔드(wasm, AArch64)는 IR만 안다.
* 코어는 하드웨어 페이지 보호, 하드웨어 폴트 전달, 호스트 스레드 정지에 기대지 않는다. 그래야 wasm과 JIT 금지 호스트에서도 같은 코드가 돈다.
* 호스트 계약은 32비트 게스트 값만 담은 이벤트와 재개다. 소비자의 기존 경계(rePIU의 `FaultEvent`, re2DJ의 `NativeImportGateEvent`)와 1:1로 대응하게 설계한다.
* 코어는 소비자 하나의 사정을 알지 않는다. 한쪽에만 필요한 것은 기능 플래그나 콜백 기본값이다.
* QEMU, Bochs, Box86, Box64, MAME, Unicorn 같은 전염성 라이선스 구현의 소스를 통합하지 않는다.
* 코드 변경 전에는 설계와 작업 계획을 문서화한다. 측정 없이 최적화하지 않는다.
* 프로젝트 버전은 `VERSION` 파일의 `major.minor.patch` 형식으로 관리하고, 소비자는 release tag를 고정해 가져간다.

## Direction

* The Intel SDM defines what a guest instruction means. A host CPU's behavior is a measuring instrument, not the definition.
* The interpreter is the correctness reference; a translation backend must produce its results.
* x86 semantics live only in the interpreter and the IR frontend. The backends (wasm, AArch64) know IR alone.
* The core never relies on hardware page protection, hardware fault delivery or stopping host threads, so the same code runs on wasm and on hosts that forbid JIT.
* The host contract is events holding 32-bit guest values only, and resumption, designed to map one to one onto the consumers' existing boundaries (rePIU's `FaultEvent`, re2DJ's `NativeImportGateEvent`).
* The core knows no single consumer's circumstances; anything one of them alone needs is a feature flag or a callback default.
* Do not integrate source from copyleft implementations such as QEMU, Bochs, Box86, Box64, MAME or Unicorn.
* Document design and work plans before changing code. Optimize nothing without a measurement.
* Manage the version in `VERSION` as `major.minor.patch`; consumers pin a release tag.

---

## 비목표

* 게임 로직, 로더, HLE, 그래픽, 입력, 오디오를 이 저장소에서 구현하지 않는다.
* 전체 PC 에뮬레이터(BIOS, 장치, 디스크)가 되지 않는다. 사용자 모드 CPU까지다.
* 소비자의 x86 데스크톱 직접 실행 경로를 대체하는 것을 목표로 두지 않는다. rePIU의 AOT 대체 가능성은 2단계 측정 뒤에 따로 판단한다.
* 원본 자산의 재배포 경로를 제공하지 않는다.

## Non-Goals

* Game logic, loaders, HLE, graphics, input and audio are not implemented in this repository.
* It does not become a full PC emulator (BIOS, devices, disks); it stops at the user-mode CPU.
* Replacing the consumers' x86 desktop direct-execution paths is not a goal. Whether rePIU's AOT could be replaced is decided separately after the phase 2 measurements.
* No redistribution path for original assets is provided.
