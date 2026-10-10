# #45 작업 지시 : wasm 번역 백엔드 / #45 work order : the wasm translation backend

이슈: [#45](https://github.com/reexec/rex86/issues/45) | 설계: [20261010-i045](../design/20261010-i045-wasm-backend.md) | 로그: [20261010-i045](../work-logs/20261010-i045-wasm-backend.md)

## 작업 항목 / Tasks

1. 공개 계약(설계 결정 1): `WasmInstall`, `WasmModuleServices`, `TranslationMode::kWasm`, `TranslationOptions::wasm`, `SetTranslation`의 반환값, `Cpu::CompleteWasmModule`, `Cpu::FailWasmModule`, `SetDefaultTranslation`.
2. `src/translate/wasm/`: `module_writer`, `codegen`, `backend`(결정 2, 3). 실행기의 대기 표와 `Release` 알림.
3. `src/host/web/`: C++ 어댑터와 JS 라이브러리, CMake `rex86_host_web`(결정 4). AGENTS.md에 `src/host/<호스트>/`의 목적.
4. 검증(결정 5): 모듈 형식 단위 테스트, wasm32 IR 차등, 번역 강제 `WASM`, 비동기 경로 테스트, CI wasm32 작업.
5. 성능: Node에서 세 엔진의 벤치마크와 설치 시간.
6. 문서: #42 설계 결정 8의 계약 이름, ARCHITECTURE, kb, 벤치마크 가이드, 작업 로그.

*1 the public contract (design decision 1): `WasmInstall`, `WasmModuleServices`, `TranslationMode::kWasm`, `TranslationOptions::wasm`, `SetTranslation`'s return value, `Cpu::CompleteWasmModule`, `Cpu::FailWasmModule` and `SetDefaultTranslation`; 2 `src/translate/wasm/` with `module_writer`, `codegen` and `backend` (decisions 2 and 3), plus the runtime's pending table and `Release` notification; 3 `src/host/web/` with the C++ adapter, the JS library and the CMake target `rex86_host_web` (decision 4), and the purpose of `src/host/<host>/` in AGENTS.md; 4 verification (decision 5): module-format unit tests, the IR differential on wasm32, forced `WASM` translation, the asynchronous path, the CI wasm32 job; 5 performance: the three engines' benchmark and installation time on Node; 6 documents: the contract names in #42's decision 8, ARCHITECTURE, the kb, the benchmark guide and the work log.*

## 완료 조건 / Completion criteria

* wasm32의 번역 강제 `WASM` 구성에서 ctest 전부가 통과하고, wasm32 IR 차등이 wasm 백엔드로 불일치 0이다.
* Node 벤치마크에 세 엔진의 첫 기록이 있다.
* 다섯 호스트 CI와 libFuzzer 작업이 녹색이다.

*All of ctest passes in wasm32's forced `WASM` configuration, and the wasm32 IR differential has zero mismatches through the wasm backend; the Node benchmark has a first record of the three engines; CI is green on the five hosts and the libFuzzer job.*
