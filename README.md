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

## 구조 / Layout

| 경로 / Path | 내용 / Contents |
|---|---|
| `include/rex86/` | 공개 계약: `cpu_state.h`, `guest_memory.h`, `environment.h`, `cpu.h`, `version.h` / the public contract |
| `src/` | 코어 구현. OS 헤더를 포함하지 않음 / the core, with no OS header |
| `src/tools/probe/` | `rex86_probe`: 모든 호스트에서 같은 줄을 찍는 확인 도구 / prints the same lines on every host |
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
