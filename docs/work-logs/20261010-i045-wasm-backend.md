# #45 작업 로그 : wasm 번역 백엔드 / #45 work log : the wasm translation backend

이슈: [#45](https://github.com/reexec/rex86/issues/45) | 설계: [20261010-i045](../design/20261010-i045-wasm-backend.md) | 지시서: [20261010-i045](../work-orders/20261010-i045-wasm-backend.md) | 상위: [#42](../design/20261010-i042-phase3-translation.md)

## 2026-10-10

호스트: AMD Ryzen 5 5600X(Zen 3) 12 스레드, Linux, GCC 13, Clang 18, emsdk 3.1.74, Node 24.19. 브랜치는 `work/i043-ir-frontend`(#43, #44 위에 쌓음).

- **맥락 확인**: `CpuState`와 `SegmentRegister`가 표준 배치이고, 필드 위치(`gpr` 0, `eip` 32, `eflags` 36, `segments` 40, 세그먼트 16바이트, `base` 4)가 wasm32와 x86-64에서 같음을 작은 프로그램으로 확인했다. 생성 코드는 `offsetof`로 위치를 정한다.
- **구현**:
  - 공개 계약: `WasmInstall`, `WasmModuleServices`(`Install`, `Release`), `TranslationMode::kWasm`, `TranslationOptions::wasm`, `bool SetTranslation`(실행할 수 없는 모드는 거절하고 꺼짐), `CompleteWasmModule`, `FailWasmModule`, `SetDefaultTranslation`, `DefaultTranslation`.
  - 실행기: 설치를 기다리는 상태와 표(ticket → 블록 머리), 버린 번역의 `Drop`, 응답이 오면 `Finished`, 머리가 없어졌으면 `Discard`. 기다리는 번역을 버려도 바이트열은 응답이 올 때까지 남긴다(계약: 응답까지 유효).
  - `src/translate/wasm/`: `module_writer`, `codegen`, `backend`. 검사는 `AccessWouldSucceed`를 부르는 C++ 도우미를 `call_indirect`로 부른다.
  - `src/host/web/`: `WebWasmModuleServices`(동기), `rex86_wasm_host.js`, `force_wasm.cpp`. CMake `rex86_host_web`, `REX86_FORCE_TRANSLATION`을 `OFF`/`ON`/`WASM` 문자열로 넓힘. AGENTS.md 구현 규칙에 `src/host/<호스트>/`.
  - IR 차등(`rex86_irdiff`)이 wasm32에서 최적화한 블록마다 wasm 백엔드로도 돌려 비교한다(`wasm_runs=`).
  - 견고성 I8의 세 번째 실행은 프로세스 기본 번역이 켜져 있으면 그 백엔드를 쓴다. 그래서 번역 강제 `WASM` 빌드에서 견고성 smoke가 wasm을 비교한다.
  - 벤치마크 `--engine wasm`과 설치 통계 줄.
  - 단위 테스트 `wasm_backend_test.cpp`: 모든 호스트에서 모듈 형식(마법수, 절 순서 1, 2, 3, 7, 10, import와 export 이름, 탈출 코드), 네이티브에서 `kWasm` 거절. wasm32에서 루프의 wasm 번역 대 인터프리터, 설치를 미루는 어댑터로 `kPending`과 `CompleteWasmModule`, `FailWasmModule`. `cpu_test`에 "기본 번역이 켜져 있으면 새 Cpu도 그 모드"를 더해, 강제 빌드가 조용히 인터프리터로 돌지 않음을 확인한다.
  - CI: wasm32 작업에 번역 강제 `WASM` 구성.
- **구현 중의 오류**: 생성자가 기본값 함수보다 앞에 있어 컴파일되지 않았다(파일 순서를 바꿈). wasm 코드 생성은 처음 빌드에서 모듈 검증과 차등을 모두 통과했다.
- **변이 시험**(wasm32, `rex86_irdiff --cases 2000`): `Sar`를 `i32.shr_u`로 바꾸면 형태별 9, 블록 6 불일치. 패리티의 `i32.eqz`를 빼면 568, 263 불일치.
- **빌드와 ctest**(`-Werror`): 10개 구성 모두 통과.

  | 구성 | 결과 |
  |---|---|
  | x86-64 Debug, Release, Clang Debug, ASan/UBSan 예열 0, 번역 강제(평가기) ASan/UBSan, Clang libFuzzer + ASan/UBSan | 각 8/8 |
  | i386 Debug, 번역 강제(평가기) Release | 각 9/9 |
  | wasm32, wasm32 번역 강제 `WASM` | 각 6/6, `rex86_probe` `result=ok` |

  - 단위 checks: 네이티브 1,988, wasm32 2,017(wasm 실행 테스트 포함), 번역 강제 `WASM` 2,018, failures 0.
  - 번역 강제 `WASM`에서 trace 묶음 18,396건 불일치 0, 견고성 300건(I8이 wasm과 비교) `result=ok`.
- **wasm32 긴 IR 차등**: Node, 4 × 25만 건(시드 45000~45003). 형태별 wasm 실행 978,683회, 블록 25만 개, 불일치 0. shard당 53초.
- **성능**(Node, 세 엔진 번갈아 세 번씩의 중앙값, [분석](../analysis/interpreter-performance.md) 2.4절): 인터프리터 대비 wasm이 alu 7.8배(26.3 → 204.7 MIPS), memory 5.2배, call 4.4배, mixed 1.4배. string, x87은 범위 밖이라 같다. 모듈 설치는 평균 88 µs(32개). 첫 프레임까지의 시간은 설치를 포함해 alu 57.0 → 15.4 ms.
- **남은 일**(측정으로 고를 최적화, #42 결정 10): 블록 묶음 설치, 블록 연결, 블록 안 레지스터 캐싱, 인라인 검사. 범위 확대(REP 문자열, x87은 지금 인터프리터). 메인 스레드용 비동기 어댑터.

*Host: AMD Ryzen 5 5600X (Zen 3), 12 threads, Linux, GCC 13, Clang 18, emsdk 3.1.74, Node 24.19; branch `work/i043-ir-frontend` (stacked on #43 and #44). Context: a small program confirmed `CpuState` and `SegmentRegister` are standard layout with the same field offsets on wasm32 and x86-64 (`gpr` 0, `eip` 32, `eflags` 36, `segments` 40, 16-byte segments, `base` 4), and generated code takes offsets from `offsetof`. Implemented: the public contract (`WasmInstall`, `WasmModuleServices` with `Install` and `Release`, `TranslationMode::kWasm`, `TranslationOptions::wasm`, `bool SetTranslation` refusing a mode that cannot run and staying off, `CompleteWasmModule`, `FailWasmModule`, `SetDefaultTranslation`, `DefaultTranslation`); in the runtime, a pending state and table (ticket to block head), `Drop` for dropped translations, `Finished` when an answer arrives and `Discard` when its head is gone, a dropped pending translation keeping its bytes until the answer (the contract keeps them valid until then); `src/translate/wasm/` with `module_writer`, `codegen` and `backend`, checks calling a C++ helper around `AccessWouldSucceed` through `call_indirect`; `src/host/web/` with `WebWasmModuleServices` (synchronous), `rex86_wasm_host.js` and `force_wasm.cpp`, the CMake target `rex86_host_web` and `REX86_FORCE_TRANSLATION` widened to the string `OFF`/`ON`/`WASM`, with `src/host/<host>/` in AGENTS.md's implementation rules; the IR differential (`rex86_irdiff`) on wasm32 also running every optimized block through the wasm backend (`wasm_runs=`); robustness I8's third run using the process's default translation when one is on, so the forced `WASM` build's robustness smoke compares wasm; the benchmark's `--engine wasm` with an installation line; unit tests in `wasm_backend_test.cpp` for the module format on every host (magic, section order 1, 2, 3, 7, 10, import and export names, exit codes) and `kWasm` refused natively, and on wasm32 the loop's wasm translation against the interpreter and a deferring adapter exercising `kPending`, `CompleteWasmModule` and `FailWasmModule`, with `cpu_test` gaining "a new Cpu starts in the default translation's mode" so a forced build cannot silently run the interpreter; CI's wasm32 job gaining the forced `WASM` configuration. Errors during implementation: the constructor preceded the default-translation function and did not compile (file order changed); the wasm code generation passed module validation and the differential on its first build. Mutation tests (wasm32, `rex86_irdiff --cases 2000`): `Sar` as `i32.shr_u` gave 9 per-form and 6 block mismatches; dropping parity's `i32.eqz` gave 568 and 263. Builds and ctest with `-Werror`: all ten configurations pass, as in the table above; unit checks 1,988 native, 2,017 on wasm32 (wasm run tests included) and 2,018 under forced `WASM`, zero failures; under forced `WASM` the 18,396-case trace corpus has zero mismatches and 300 robustness cases (I8 against wasm) end `result=ok`. Long wasm32 IR differential on Node, 4 × 250,000 cases (seeds 45000-45003): 978,683 per-form wasm runs and 250,000 blocks with zero mismatches, 53 s per shard. Performance (Node, medians of three interleaved runs of the three engines, section 2.4 of the analysis): wasm against the interpreter is 7.8 times on alu (26.3 to 204.7 MIPS), 5.2 on memory, 4.4 on call and 1.4 on mixed, string and x87 unchanged being outside the coverage; module installation averaged 88 µs over 32; the time to the first frame, installation included, went from 57.0 to 15.4 ms on alu. Remaining (optimizations chosen by measurement, #42 decision 10): batched installation, block chaining, register caching within blocks and inline checks; wider coverage (REP strings and the x87 run on the interpreter today); an asynchronous adapter for the main thread.*
