# #48 작업 로그 : 브라우저 검증, 비동기 설치 어댑터, 브라우저 CI / #48 work log : browser validation, the asynchronous installer, browser CI

이슈: [#48](https://github.com/reexec/rex86/issues/48) | 설계: [20261011-i048](../design/20261011-i048-browser-validation.md) | 지시서: [20261011-i048](../work-orders/20261011-i048-browser-validation.md) | 기록: [브라우저의 wasm 번역](../analysis/browser-wasm.md)

## 2026-10-11

호스트: AMD Ryzen 5 5600X(Zen 3) 12 스레드, Linux, GCC 13, emsdk 3.1.74, Node 24.19, Playwright 1.63.0(헤드리스 Chromium 153.0.8010.12, Firefox 155.0). 브랜치는 `work/i048-browser-validation`(main `2afcabc`에서).

- **사전 실험**(설계 "확인됨"): Chromium과 Firefox의 Worker에서 `rex86_irdiff --cases 2000`이 Node와 같은 줄을 냈다. 조건은 `-sEXIT_RUNTIME=1`과 `locateFile`이다. WebKit은 이 기계에 시스템 라이브러리가 없어 뜨지 않는다. CI의 `--with-deps`에 맡긴다.
- **구현**:
  - CMake:
    - `REX86_BROWSER`(Emscripten에서만): `-sEXIT_RUNTIME=1`, `rex86_trace`와 `rex86_bench`에서 `-sNODERAWFS` 뺌, `--embed-file tests/traces@/traces`.
    - trace 목록 `bin/rex86_browser.json`을 만들고, ctest는 등록하지 않는다.
    - 프리셋 `web-browser-release`를 더했다.
  - `AsyncWebWasmModuleServices`와 JS 쪽 `rex86_wasm_install_async`, `rex86_wasm_take`, `rex86_wasm_forget`. 결과 큐는 어댑터(`this`)마다 두고, 소멸자가 넘기지 않은 결과의 테이블 칸을 돌려준다.
  - `tests/host/web/`: `async_test.cpp`(`Cpu` 둘, 코드 주소 0x1000과 0x2000, 같은 `emscripten_set_main_loop`), `index.html`, `suite.js`, `worker.js`, `main.html`, `report.mjs`, `serve.mjs`, `drive.mjs`, `package.json`, `package-lock.json`. `rex86_async_test`는 wasm32 ctest(Node)에도 등록했다. `node_modules/`는 `.gitignore`에 넣었다.
  - CI 작업 `browsers`.
  - 설계에서 더한 것:
    - `main.html`: 메인 스레드 도구를 페이지 안 frame에 띄운다. 페이지의 전역 `Module`과 섞이지 않게 하기 위해서다.
    - `report.mjs`: 페이지와 드라이버가 같은 보고 줄을 쓴다.
    - 테스트 사이 대기, 메모리 부족 재시도, 브라우저 IR 차등 5,000건(아래 발견 2).
- **발견 1**(Node에서 처음 실행): JS 라이브러리의 `$rex86WasmQueues: new Map()`이 생성 JS에 `{}`로 직렬화되어 `rex86WasmQueues.get is not a function`이 났다. 문자열 `'new Map()'`로 고쳤다(kb에 적음).
- **발견 2**(Firefox): 처음 모음 실행에서 `unit` 바로 뒤의 `irdiff`와 `robust`가 주 `.wasm` 인스턴스화에서 `InternalError: out of memory`로 실패했다. 두 도구 모두 혼자 돌리면 통과했고, 실패한 `robust`는 번역을 쓰지 않는다. 좁혀 본 결과는 다음과 같다(기록 3절).
  - 단위 테스트의 최대 RSS는 171 MB로, 힙 문제는 아니다.
  - 테스트 사이에 3초 이상 기다리면 통과한다.
  - `irdiff --cases 200000`은 Firefox에서 설치 거절 9건을 냈고, `mismatches=` 합 9와 같다. 번역 결과가 틀린 경우는 아니다.
  - 작은 모듈을 계속 만들면 16,349번째에 실패하고(`failed to allocate executable memory for module`), 콘솔에 그 경고가 남는다. 참조를 버려도, 100개마다 16 ms씩 제어를 돌려줘도 같다. Chromium은 10만 개에서도 실패하지 않았다.
  - 대기 0에서는 15초씩 두 번 다시 시작해도 회복하지 못했다. 실패한 실행이 모듈을 더 남기기 때문이다.
  - 그래서 대기 5초, 브라우저 IR 차등 5,000건, "메모리 부족만으로 실패"한 실행의 재시도(두 번, 15초)로 정했다. 재시도 횟수는 보고에 남긴다.
  - 소비자 영향: 블록 하나에 모듈 하나인 지금의 백엔드는 Firefox에서 모듈 1만 6천 개 남짓으로 묶인다. 묶음 설치의 근거로 후속 이슈를 제안한다.
- **검증**:
  - 비동기 테스트(Node): `guests=2 frames=61 installed=12 failed=0 block_runs=580542 translated_steps=2321490 ran_while_pending=1 result=ok`.
  - 변이 시험: `rex86_wasm_take`가 owner를 무시하고 아무 큐에서나 꺼내게 하면 `result=fail reason=no installed translation ran`으로 잡힌다. 되돌린 뒤 통과했다.
  - 브라우저 모음: Chromium 10/10을 다섯 번, Firefox 10/10을 대기 5초로 여섯 번 통과했고, 재시도는 0이었다. 두 브라우저의 메인 스레드 비동기 테스트 줄이 Node와 같다.
  - 처리량(세 번 번갈아 돌린 중앙값, 기록 4절): wasm/인터프리터 alu 8.9(Chromium)와 9.9(Firefox), call 4.7과 3.8. Chromium의 설치는 모듈당 100 µs다. Firefox의 설치 시간은 `performance.now()`의 1 ms 단위 때문에 쓰지 않는다.
  - ctest 전체 통과: 보통 wasm32 7/7(새 `rex86_async_test` 포함), 번역 강제 `WASM` 7/7, 네이티브 x86-64 Release 8/8. 모두 `-DREX86_WARNINGS_AS_ERRORS=ON`이다.

*Host: AMD Ryzen 5 5600X (Zen 3), 12 threads, Linux, GCC 13, emsdk 3.1.74, Node 24.19, Playwright 1.63.0 (headless Chromium 153.0.8010.12, Firefox 155.0); branch `work/i048-browser-validation` (from main `2afcabc`). Spike (the design's "confirmed"): `rex86_irdiff --cases 2000` printed Node's lines in Workers of Chromium and Firefox given `-sEXIT_RUNTIME=1` and `locateFile`; WebKit lacks system libraries here and is left to CI's `--with-deps`. Implemented: in CMake, `REX86_BROWSER` (Emscripten only: `-sEXIT_RUNTIME=1`, no `-sNODERAWFS` for `rex86_trace` and `rex86_bench`, `--embed-file tests/traces@/traces`, the trace list `bin/rex86_browser.json`, no ctest) and the preset `web-browser-release`; `AsyncWebWasmModuleServices` with the JS `rex86_wasm_install_async`, `rex86_wasm_take` and `rex86_wasm_forget`, the result queue kept per adapter (`this`) and the destructor giving back the table slots of results not delivered; `tests/host/web/` (`async_test.cpp` with two `Cpu`s, code at 0x1000 and 0x2000, on one `emscripten_set_main_loop`, plus `index.html`, `suite.js`, `worker.js`, `main.html`, `report.mjs`, `serve.mjs`, `drive.mjs`, `package.json`, `package-lock.json`), `rex86_async_test` also in wasm32's ctest (Node), `node_modules/` in `.gitignore`; the CI job `browsers`. Added beyond the design: `main.html`, starting the main-thread tool in a frame inside the page so it does not mix with the page's global `Module`; `report.mjs`, the page and the driver sharing report lines; the wait between tests, the out-of-memory retry and 5,000 IR differential cases in browsers (finding 2 below). Finding 1 (the first Node run): the JS library's `$rex86WasmQueues: new Map()` was serialized into the generated JS as `{}`, failing with `rex86WasmQueues.get is not a function`; fixed with the string `'new Map()'` (in the kb). Finding 2 (Firefox): in the first suite run, `irdiff` and `robust` right after `unit` failed instantiating their main `.wasm` with `InternalError: out of memory`, while each passed alone and `robust` uses no translation. Narrowed down (record section 3): the unit tests peak at 171 MB RSS, so not the heap; a wait of 3 seconds or more between tests passes; `irdiff --cases 200000` had 9 installations refused in Firefox, matching its `mismatches=` total of 9, not wrong translations; small modules fail at the 16,349th (`failed to allocate executable memory for module`, the console warning), held or dropped, yielding for 16 ms every 100 or not, while Chromium did not fail at 100,000; with no wait, two restarts 15 seconds apart did not recover, the failed runs leaving more modules. Hence the 5-second wait, 5,000 IR differential cases in browsers and retries (twice, 15 seconds) of a run that failed for want of memory alone, counted in the report. Consumer impact: the current one-module-per-block backend is bounded in Firefox by some 16,000 modules, grounding a follow-up issue for batched installation. Verification: the asynchronous test on Node, `guests=2 frames=61 installed=12 failed=0 block_runs=580542 translated_steps=2321490 ran_while_pending=1 result=ok`; mutation test: with `rex86_wasm_take` ignoring the owner and taking from any queue it fails with `result=fail reason=no installed translation ran`, passing again once restored; the browser suite: Chromium 10/10 five times and Firefox 10/10 six times with the 5-second wait, zero retries, the main-thread asynchronous test's line in both browsers equal to Node's; throughput (medians of three interleaved runs, record section 4): wasm over the interpreter at 8.9 (Chromium) and 9.9 (Firefox) on alu and 4.7 and 3.8 on call, Chromium installing at 100 µs per module, Firefox's installation time not used for `performance.now()`'s 1 ms granularity; the whole ctest passing with `-DREX86_WARNINGS_AS_ERRORS=ON` in the ordinary wasm32 configuration (7/7, the new `rex86_async_test` included), forced `WASM` (7/7) and native x86-64 Release (8/8).*

- **CI**([실행 38067728830](https://github.com/reexec/rex86/actions/runs/38067728830), 커밋 `5eeab82`): 모든 작업 녹색. `browsers`에서 Chromium 153, Firefox 155, WebKit 26.6이 모두 10/10, 재시도 0. WebKit의 비동기 테스트는 `installed=12 failed=0 ran_while_pending=1`이고, `block_runs` 570,550은 설치가 도착한 프레임에 따라 달라지는 값이다. WebKit 결과를 기록 1절에 더했다.

*CI (run 38067728830, commit `5eeab82`): every job green; in `browsers`, Chromium 153, Firefox 155 and WebKit 26.6 each 10/10 with zero retries, WebKit's asynchronous test printing `installed=12 failed=0 ran_while_pending=1`, its 570,550 `block_runs` depending on the frame the installations arrived in; WebKit's result added to record section 1.*

### 이어서 할 일 / Next

1. 실기기(모바일 Chrome, iOS Safari, macOS Safari)는 사용자가 가이드 3절로 돌린 뒤 기록 5절에 더한다.
2. 후속 이슈 제안: 여러 블록을 모듈 하나로 묶는 설치와 모듈 수 예산(기록 3절).
3. 머지는 사용자 요청 때.

*Next: 1 real devices (mobile Chrome, iOS Safari, macOS Safari) join record section 5 once the user runs guide section 3; 2 propose a follow-up issue for installing several blocks per module and a module count budget (record section 3); 3 merge on the user's request.*
