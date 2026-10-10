# #45 설계 : wasm 번역 백엔드 / #45 design : the wasm translation backend

이슈: [#45](https://github.com/reexec/rex86/issues/45) | 상위 설계: [#42](20261010-i042-phase3-translation.md) 결정 7, 8, 11 | 앞 이슈: [#43](20261010-i043-ir-frontend.md), [#44](20261010-i044-translation-runtime.md) | kb: [wasm에서 실행 중에 만든 코드](../kb/wasm-runtime-code.md) | 지시서: [20261010-i045](../work-orders/20261010-i045-wasm-backend.md) | 로그: [20261010-i045](../work-logs/20261010-i045-wasm-backend.md)

IR 블록을 wasm 모듈로 내리는 백엔드, 호스트가 모듈을 설치하는 공개 계약, `src/host/web/`의 참조 어댑터를 만든다. 번역 실행기(#44)에는 백엔드 하나가 더해질 뿐이다.

*The backend lowering IR blocks to wasm modules, the public contract through which the host installs them, and the reference adapter under `src/host/web/`; the runtime (#44) just gains a backend.*

## 결정 1: 공개 계약 / Decision 1: the public contract

```cpp
// include/rex86/environment.h
enum class WasmInstall : std::uint8_t { kInstalled, kPending, kFailed };

class WasmModuleServices
{
public:
    virtual ~WasmModuleServices() = default;
    // Compiles and instantiates the module against the core's own memory
    // (env.memory) and function table (env.table), and adds its exports, in
    // order, to the table. kInstalled: table_indices holds export_count
    // indices now. kPending: the host answers later through
    // Cpu::CompleteWasmModule or Cpu::FailWasmModule with this ticket, on
    // the Cpu's thread and outside Run; the bytes stay valid until then.
    virtual WasmInstall Install(std::uint32_t ticket, const std::uint8_t* bytes, std::size_t size,
                                std::uint32_t export_count, std::uint32_t* table_indices) = 0;
    // The core no longer calls these table entries.
    virtual void Release(const std::uint32_t* table_indices, std::uint32_t count) = 0;
};
```

* `TranslationMode::kWasm`과 `TranslationOptions::wasm`(`WasmModuleServices*`)을 더한다. `kWasm`은 wasm32 빌드에서 `wasm`이 null이 아닐 때만 켜진다. 그렇지 않으면 `SetTranslation`이 꺼짐으로 남기고 false를 돌려준다(조용히 성공하는 더미를 두지 않는다, AGENTS.md).
* `Cpu::CompleteWasmModule(ticket, indices, count)`와 `Cpu::FailWasmModule(ticket)`: 비동기 호스트의 응답.
* `SetDefaultTranslation(const TranslationOptions&)`: 그 뒤에 만드는 모든 `Cpu`의 기본 번역 설정(프로세스 전체). 번역 강제 테스트가 이것으로 wasm 백엔드를 켠다(결정 5). 소비자에게도 "모든 Cpu에 번역" 한 줄이 된다.
* #42 결정 8의 `Submit`을 `Install`로 바꿨다. 동기 호스트(Node, Worker의 `new WebAssembly.Module`)가 결과를 바로 돌려주면 왕복이 하나 줄고, 비동기 호스트는 `kPending`으로 같은 계약을 쓴다. 테이블 칸을 돌려받는 `Release`를 더했다.

*`TranslationMode::kWasm` and `TranslationOptions::wasm` (a `WasmModuleServices*`) are added; `kWasm` turns on only in a wasm32 build with a non-null `wasm`, and otherwise `SetTranslation` leaves translation off and returns false (no silently succeeding dummy, AGENTS.md). `Cpu::CompleteWasmModule(ticket, indices, count)` and `Cpu::FailWasmModule(ticket)` answer for an asynchronous host. `SetDefaultTranslation(const TranslationOptions&)` sets the default for every `Cpu` made afterwards, process-wide; forced-translation tests turn the wasm backend on with it (decision 5), and for consumers it is one line for "translate on every Cpu". #42 decision 8's `Submit` became `Install`: a synchronous host (Node, `new WebAssembly.Module` in a Worker) answering at once saves a round trip, and an asynchronous host uses the same contract through `kPending`; `Release` was added to hand table slots back.*

## 결정 2: 생성 코드 / Decision 2: generated code

* **함수 모양**: `(param state i32) (param memory i32) (param base i32) (result i32)`. `state`는 `CpuState*`, `memory`는 `GuestMemory*`, `base`는 게스트 주소 0의 선형 메모리 주소다. 상수로 박지 않고 인자로 받으므로 호스트가 메모리나 속성표를 옮겨도 번역이 낡지 않는다.
* **값**: IR 값마다 i32 지역 변수 하나. 레지스터와 플래그는 `CpuState`를 직접 읽고 쓴다(#42 결정 4의 첫 판). 위치는 C++의 `offsetof`로 정한다.
* **연산**: 대부분 wasm 명령 하나다. `MulHiS`/`MulHiU`는 i64 곱의 상위, `Parity`는 `popcnt`, `Select`는 `select`, 시프트는 wasm이 개수를 31로 마스크하는 것이 IR의 정의와 같다.
* **검사**: 코어의 C++ 함수 `AccessWouldSucceed`를 `call_indirect`로 부른다. wasm32에서 C++ 함수 포인터는 같은 테이블의 인덱스이므로, 모듈이 `env.table`을 import하면 평가기와 **같은 코드**가 검사를 판정한다. 인라인 빠른 경로는 측정 뒤의 최적화다.
* **메모리 접근**: 검사를 통과한 뒤 `base + seg.base + off`에 `i32.load8_u`/`load16_u`/`load`, `store8`/`store16`/`store`를 바로 쓴다. 게스트는 리틀 엔디안이고 wasm도 그렇다. 정렬되지 않은 접근도 wasm에서 맞다.
* **탈출**: EIP를 `CpuState`에 쓰고 `(steps << 1) | kind`를 돌려준다.
* **모듈**: 첫 판은 블록 하나에 모듈 하나다(export `b`). #42 결정 7의 묶음은 설치 비용을 잰 뒤에 더한다.

*Function shape: `(param state i32) (param memory i32) (param base i32) (result i32)`, `state` a `CpuState*`, `memory` a `GuestMemory*`, `base` the linear address of guest address 0; taking them as arguments rather than constants keeps translations from going stale when the host moves memory or the attribute table. Values: one i32 local per IR value; registers and flags are read and written in `CpuState` directly (#42 decision 4's first version), at offsets from C++'s `offsetof`. Operations are mostly one wasm instruction each: `MulHiS`/`MulHiU` take the high half of an i64 product, `Parity` uses `popcnt`, `Select` is `select`, and wasm masking shift counts to 31 matches the IR's definition. Checks call the core's C++ `AccessWouldSucceed` through `call_indirect`: on wasm32 a C++ function pointer is an index into the same table, so with `env.table` imported the evaluator's own code judges the check; an inline fast path is an optimization after measurement. Memory access, after the check, loads and stores at `base + seg.base + off` directly, guest and wasm both little-endian and unaligned access correct in wasm. An exit writes EIP into `CpuState` and returns `(steps << 1) | kind`. The first version makes one module per block (export `b`); #42 decision 7's batching follows a measurement of installation cost.*

## 결정 3: 백엔드와 실행기 / Decision 3: the backend and the runtime

* `src/translate/wasm/`: `module_writer`(wasm 이진 형식의 최소 인코더), `codegen`(IR → 함수 본문), `backend`(`Backend` 구현). 이식 가능한 C++이다. 모듈 바이트열은 어느 호스트에서도 만들 수 있고, 실행만 wasm32에서 한다.
* `Compile`은 `Install`을 부른다. `kInstalled`면 인덱스를 `Translation::handle`에 두고 준비됨, `kPending`이면 실행기가 표를 들고 기다린다, `kFailed`면 번역 불가.
* 실행기는 번역을 버릴 때(세대 변경, `Flush`, 한도) 백엔드에 알리고, 백엔드는 `Release`로 테이블 칸을 돌려준다.
* `Run`은 인덱스를 함수 포인터로 바꿔 부른다(wasm32에서만 컴파일되는 한 줄).

*`src/translate/wasm/` holds `module_writer` (a minimal encoder of the wasm binary format), `codegen` (IR to a function body) and `backend` (the `Backend` implementation), portable C++: module bytes can be made on any host and run only on wasm32. `Compile` calls `Install`: `kInstalled` puts the index in `Translation::handle` and is ready, `kPending` leaves the runtime holding the ticket to wait, `kFailed` makes the head untranslatable. When the runtime drops a translation (a generation change, `Flush`, the cap) it tells the backend, which hands the table slot back through `Release`. `Run` turns the index into a function pointer and calls it, one line compiled on wasm32 only.*

## 결정 4: 참조 어댑터 / Decision 4: the reference adapter

* `src/host/web/wasm_module_services.{h,cpp}`: `WasmModuleServices`의 동기 구현. `.cpp`는 첫머리에서 `__EMSCRIPTEN__`이 아니면 `#error`다(AGENTS.md: 호스트 전용 파일).
* `src/host/web/rex86_wasm_host.js`: Emscripten JS 라이브러리. `new WebAssembly.Module`, `new WebAssembly.Instance(module, { env: { memory: wasmMemory, table: wasmTable } })`, `addFunction`, `removeFunction`.
* 링크 조건: `--js-library src/host/web/rex86_wasm_host.js`, `-sALLOW_TABLE_GROWTH`. CMake 타깃 `rex86_host_web`(Emscripten에서만)가 이 둘을 실어 나른다.
* 동기 설치는 Node와 Worker에서 쓸 수 있다. 메인 스레드의 큰 모듈은 비동기여야 한다([kb](../kb/wasm-runtime-code.md)). 그 경우를 위한 비동기 어댑터는 이 이슈의 범위 밖이고, 계약(`kPending`)은 이미 받는다.
* AGENTS.md 구현 규칙에 `src/host/<호스트>/`의 목적을 더한다: 소비자가 가져다 쓰는 호스트 어댑터, 코어 라이브러리 밖, 호스트 확인 `#error`.

*`src/host/web/wasm_module_services.{h,cpp}` is a synchronous `WasmModuleServices`, its `.cpp` opening with an `#error` unless `__EMSCRIPTEN__` (AGENTS.md: host-specific files); `src/host/web/rex86_wasm_host.js` is an Emscripten JS library using `new WebAssembly.Module`, `new WebAssembly.Instance(module, { env: { memory: wasmMemory, table: wasmTable } })`, `addFunction` and `removeFunction`. Linking needs `--js-library src/host/web/rex86_wasm_host.js` and `-sALLOW_TABLE_GROWTH`, carried by the CMake target `rex86_host_web` (Emscripten only). Synchronous installation works in Node and Workers; large modules on the main thread must be asynchronous (kb), an asynchronous adapter for that being outside this issue while the contract (`kPending`) already takes it. AGENTS.md's implementation rules gain the purpose of `src/host/<host>/`: host adapters consumers take, outside the core library, with the host-checking `#error`.*

## 결정 5: 검증 / Decision 5: verification

| 수단 | 내용 |
|---|---|
| IR 차등 | wasm32에서 `rex86_irdiff`가 평가기에 더해 wasm 백엔드로도 같은 블록을 돌려 인터프리터와 비교한다 |
| 번역 강제 | CMake `REX86_FORCE_TRANSLATION`을 `OFF`/`ON`(평가기)/`WASM`으로 넓힌다. `WASM`은 Emscripten에서 테스트 도구에 `rex86_host_web`과 작은 초기화 파일(`SetDefaultTranslation({kWasm, 0, 어댑터})`)을 링크한다. ctest 전부(단위 테스트, trace 묶음, 견고성 smoke와 I8, 벤치마크 smoke, IR 차등)가 wasm 번역으로 돈다 |
| 비동기 경로 | wasm32 단위 테스트에서 `kPending`을 돌려주는 어댑터로 설치를 미루고 `CompleteWasmModule`, `FailWasmModule` 뒤의 동작을 본다 |
| 모듈 형식 | 모든 호스트의 단위 테스트에서 작은 블록의 모듈 바이트열이 기대한 구조(마법수, 절 순서, import, export)를 갖는지 |
| CI | wasm32 작업에 번역 강제 `WASM` 구성을 더한다 |
| 성능 | Node에서 `rex86_bench --engine interpreter|evaluator|wasm`. 블록 설치 시간과 첫 프레임까지의 시간 |

*The IR differential on wasm32 also runs every block through the wasm backend besides the evaluator and compares with the interpreter. Forced translation: CMake's `REX86_FORCE_TRANSLATION` widens to `OFF`/`ON` (evaluator)/`WASM`, `WASM` linking `rex86_host_web` and a small initializer (`SetDefaultTranslation({kWasm, 0, adapter})`) into the test tools under Emscripten, so all of ctest (unit tests, trace corpus, robustness smoke with I8, benchmark smoke, IR differential) runs on wasm translation. The asynchronous path: a wasm32 unit test with an adapter answering `kPending` defers installation and checks behavior after `CompleteWasmModule` and `FailWasmModule`. Module format: unit tests on every host check a small block's module bytes for the expected structure (magic, section order, imports, exports). CI: the wasm32 job gains the forced `WASM` configuration. Performance: `rex86_bench --engine interpreter|evaluator|wasm` on Node, with block installation time and time to first frame.*

## 소비자 영향 / Consumer impact

* 공개 계약에 추가만 있다: `WasmInstall`, `WasmModuleServices`, `TranslationMode::kWasm`, `TranslationOptions::wasm`, `Cpu::CompleteWasmModule`, `Cpu::FailWasmModule`, `SetDefaultTranslation`.
* 웹 경로(rePIU의 설계 513, re2DJ의 웹 빌드): Worker에서 `rex86_host_web`을 링크하고 `SetTranslation({kWasm, 32, &adapter})`를 부르면 번역이 켜진다. 링크 플래그 `-sALLOW_TABLE_GROWTH`가 필요하다.
* 네이티브 경로는 바뀌지 않는다.

*The public contract only gains `WasmInstall`, `WasmModuleServices`, `TranslationMode::kWasm`, `TranslationOptions::wasm`, `Cpu::CompleteWasmModule`, `Cpu::FailWasmModule` and `SetDefaultTranslation`. The web paths (rePIU's design 513, re2DJ's web build) turn translation on by linking `rex86_host_web` in their Worker and calling `SetTranslation({kWasm, 32, &adapter})`, with the link flag `-sALLOW_TABLE_GROWTH`. Native paths do not change.*
