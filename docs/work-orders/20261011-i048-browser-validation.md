# #48 작업 지시 : 브라우저 검증, 비동기 설치 어댑터, 브라우저 CI / #48 work order : browser validation, the asynchronous installer, browser CI

이슈: [#48](https://github.com/reexec/rex86/issues/48) | 설계: [20261011-i048](../design/20261011-i048-browser-validation.md) | 로그: [20261011-i048](../work-logs/20261011-i048-browser-validation.md)

## 작업 항목 / Tasks

1. CMake `REX86_BROWSER`: `-sNODERAWFS` 없이, `-sEXIT_RUNTIME=1`, trace 묶음 내장(설계 결정 1).
2. `AsyncWebWasmModuleServices`와 JS 쪽 비동기 설치, 결과 큐(결정 2).
3. `tests/host/web/`: 실행기 페이지, Worker 부트스트랩, 정적 서버, Playwright 드라이버, `async_test.cpp`, Playwright 고정(결정 3). `async_test`는 wasm32 ctest(Node)에도 등록한다.
4. CI 작업 `browsers`(결정 4).
5. 로컬 검증: Chromium과 Firefox에서 모음 전체, Node에서 비동기 테스트.
6. 문서: 가이드 `browser-tests.md`, `THIRD_PARTY_NOTICES.md`, 분석(브라우저별 결과와 성능), ARCHITECTURE, kb, 작업 로그.

*1 CMake `REX86_BROWSER`: no `-sNODERAWFS`, `-sEXIT_RUNTIME=1`, the trace corpus embedded (design decision 1); 2 `AsyncWebWasmModuleServices` with asynchronous installation and a result queue on the JS side (decision 2); 3 `tests/host/web/`: the runner page, the Worker bootstrap, the static server, the Playwright driver, `async_test.cpp` and the pinned Playwright (decision 3), `async_test` also registered in wasm32's ctest (Node); 4 the CI job `browsers` (decision 4); 5 local verification: the whole suite in Chromium and Firefox, the asynchronous test on Node; 6 documents: the guide `browser-tests.md`, `THIRD_PARTY_NOTICES.md`, the analysis (results and performance per browser), ARCHITECTURE, the kb, the work log.*

## 완료 조건 / Completion criteria

* CI의 `browsers` 작업이 Chromium, Firefox, WebKit에서 모음 전체를 통과한다.
* 비동기 어댑터 테스트가 Node와 브라우저 메인 스레드에서 통과한다.
* 실기기 절차가 가이드에 있다. 실기기 결과는 사용자가 돌린 뒤 분석에 더한다.
* 다섯 호스트 CI와 libFuzzer 작업이 녹색이다.

*CI's `browsers` job passes the whole suite in Chromium, Firefox and WebKit; the asynchronous adapter's test passes on Node and on a browser's main thread; the guide holds the real-device procedure, real-device results joining the analysis once the user has run them; CI is green on the five hosts and the libFuzzer job.*
